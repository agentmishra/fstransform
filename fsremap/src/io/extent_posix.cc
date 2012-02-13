/*
 * io/extent_posix.cc
 *
 *  Created on: Feb 27, 2011
 *      Author: max
 */

#include "../first.hh" // for FT_*TEMPLATE* macros */

#if defined(FT_HAVE_ERRNO_H)
# include <errno.h>        // for errno, ENOMEM, EINVAL, EFBIG
#elif defined(FT_HAVE_CERRNO)
# include <cerrno>         // for errno, ENOMEM, EINVAL, EFBIG
#endif
#if defined(FT_HAVE_STDLIB_H)
# include <stdlib.h>       // for malloc(), free()
#elif defined(FT_HAVE_CSTDLIB)
# include <cstdlib>        // for malloc(), free()
#endif
#if defined(FT_HAVE_STRING_H)
# include <string.h>       // for memset()
#elif defined(FT_HAVE_CSTRING)
# include <cstring>        // for memset()
#endif

#ifdef FT_HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>    // for ioctl()
#endif

#ifdef FT_HAVE_LINUX_FS_H
# include <linux/fs.h>     // for FS_IOC_FIEMAP, FIBMAP */
#endif
#ifdef FT_HAVE_LINUX_FIEMAP_H
 /* if <linux/fs.h> defines FS_IOC_FIEMAP, <linux/fiemap.h> is supposed to exist */
# include <linux/fiemap.h> // for struct fiemap and struct fiemap_extent. 
#endif

#include <utility>         // for std::pair<T1,T2> and std::make_pair()
#include <vector>          // for std::vector<T>


#include "../log.hh"       // for ff_log() */
#include "../traits.hh"    // for FT_TYPE_TO_UNSIGNED(T) */
#include "../types.hh"     // for ft_off */
#include "../extent.hh"    // for fr_extent<T>, fr_map<T>, ff_filemap() */
#include "../vector.hh"    // for fr_vector<T> */
#include "extent_posix.hh" // for ff_read_extents_posix() */
#include "util_posix.hh"   // for ff_posix_ioctl(), ff_posix_size() */

FT_IO_NAMESPACE_BEGIN

/**
 * retrieves file blocks allocation map (extents) for specified file descriptor
 * and appends them to ret_vector (with user_data = FC_DEFAULT_USER_DATA).
 * in case of failure returns errno-compatible error code, and ret_vector contents will be UNDEFINED.
 *
 * must (and will) also check that device blocks count can be represented by ret_list,
 * by calling ret_list.extent_set_range(block_size, block_count)
 *
 * implementation: calls ioctl(FIBMAP)
 */
static int ff_posix_fibmap(int fd, ft_uoff dev_length, fr_vector<ft_uoff> & ret_list, ft_uoff & ret_block_size_bitmask)
{
#ifdef FIBMAP
    ft_uoff file_length, file_block_count, dev_block_count;
    ft_uoff block_size, logical_uoff, physical_uoff, block_size_bitmask = ret_block_size_bitmask;

    /* lower-level API ff_posix_ioctl(FIGETBSZ) and ff_posix_ioctl(FIBMAP) need these to be int */
    int err = 0, block_size_int, logical, physical;

    do {
        if ((err = ff_posix_ioctl(fd, FIGETBSZ, & block_size_int))) {
            err = ff_log(FC_ERROR, err, "ff_posix_fibmap(): error in ioctl(%d, FIGETBSZ)", fd);
            break;
        }

        block_size = (ft_uoff) block_size_int;
        // ft_uoff is expected to be unsigned and wider than int,
        // but explicitly checking for overflow is always safer than "expecting"
        if (block_size < 0 || block_size_int != (int) block_size) {
            /* overflow! give up. */
            err = ff_log(FC_ERROR, EFBIG, "ff_posix_fibmap(): error, block_size = %"FT_ULL" overflows type (ft_uoff)", (ft_ull) block_size_int);
            break;
        }
        /* keep track of bits used by extents. needed to compute effective block size */
        block_size_bitmask |= block_size;

        if ((err = ff_posix_size(fd, & file_length))) {
            err = ff_log(FC_ERROR, err, "ff_posix_fibmap(): error in stat(%d)", fd);
            break;
        }

        // number of blocks in the device
        dev_block_count = (dev_length + block_size - 1) / block_size;

        // number of blocks in the file
        file_block_count = (file_length + block_size - 1) / block_size;

        // ioctl(FIBMAP) wants an (int logical) and returns an (int physical)
        // in units of one block, so we must check for overflow!
        int n = (int) file_block_count;
        int m = (int) dev_block_count;

        if (m < 0 || dev_block_count != (ft_uoff) m
            || n < 0 || file_block_count != (ft_uoff) n)
        {
            /* overflow! give up. */
            err = ff_log(FC_ERROR, EFBIG, "ff_posix_fibmap(): error, dev_block_count = %"FT_ULL", file_block_count = %"FT_ULL" overflow type (int)",
                         fd, (ft_ull) dev_block_count, (ft_ull) file_block_count);
            break;
        }

        for (logical = 0; logical < n; logical++) {
            physical = logical;
            if ((err = ff_posix_ioctl(fd, FIBMAP, & physical))) {
                err = ff_log(FC_ERROR, err, "ff_posix_fibmap(): error in ioctl(%d, FIBMAP, %"FT_ULL")", fd, (ft_ull) logical);
                break;
            }
            /* FIBMAP reports holes (i.e. unallocated blocks in the file) as physical == 0. ugly */
            if (physical != 0) {
                physical_uoff = (ft_uoff) physical * block_size;
                logical_uoff = (ft_uoff) logical * block_size;

                /* keep track of bits used by extents. needed to compute effective block size */
                ret_block_size_bitmask |= physical_uoff | logical_uoff;

                /* this is painful... FIBMAP reports one block per call */
                ret_list.append(physical_uoff, logical_uoff, block_size, FC_DEFAULT_USER_DATA);
            }
        }
    } while (0);

    if (err == 0) {
        ff_log(FC_DEBUG, 0, "ioctl(%d, FIBMAP) succeeded", fd);
        ret_block_size_bitmask = block_size_bitmask;
    }
    return err;
#else
    return ENOSYS;
#endif /* FIBMAP */
}


#ifdef FS_IOC_FIEMAP
static int ff_linux_fiemap_ioctl(int fd, ft_uoff file_length, ft_u32 extent_n, struct fiemap ** ret_k_map) {
    struct fiemap * k_map;
    ft_size k_len = sizeof(struct fiemap) + extent_n * sizeof(struct fiemap_extent);
    int err = 0;

    do {
        k_map = (struct fiemap *) malloc(k_len);
        if (k_map == NULL) {
            err = ENOMEM; /* Out of memory */
            /* do not mark the error as reported, this is just a DEBUG message */
            ff_log(FC_DEBUG, 0, "malloc(%"FT_ULL") failed (%s), falling back on ioctl(FIBMAP) ...", (ft_ull) k_len, strerror(err));
            break;
        }
        memset(k_map, 0, k_len);

        k_map->fm_start = 0L;
        k_map->fm_length = (ft_u64) file_length;
        k_map->fm_flags = FIEMAP_FLAG_SYNC;
        k_map->fm_extent_count = extent_n;

        if ((err = ff_posix_ioctl(fd, FS_IOC_FIEMAP, k_map)) != 0) {
            /* do not mark the error as reported, this is just a DEBUG message */
            ff_log(FC_DEBUG, 0, "ioctl(%d, FS_IOC_FIEMAP, extents[%"FT_ULL"]) failed (%s), falling back on ioctl(FIBMAP) ...", fd, (ft_ull) extent_n, strerror(err));
        }
    } while (0);

    if (err != 0) {
        if (k_map != NULL) {
            free(k_map);
            k_map = NULL;
        }
    }

    * ret_k_map = k_map;
    return err;
}
#endif /* FS_IOC_FIEMAP */

/*
 * retrieves file blocks allocation map (extents) for specified file descriptor
 * and appends them to ret_vector (with user_data = FC_DEFAULT_USER_DATA).
 * in case of failure returns errno-compatible error code and ret_vector contents will be UNCHANGED.
 *
 * must (and will) also check that device blocks count can be represented by ret_list,
 * by calling ret_list.extent_set_range(block_size, block_count)
 *
 * implementation: calls ioctl(FS_IOC_FIEMAP)
 */
static int ff_linux_fiemap(int fd, fr_vector<ft_uoff> & ret_list, ft_uoff & ret_block_size_bitmask)
{
#ifdef FS_IOC_FIEMAP
    struct fiemap * k_map = NULL;
    struct fiemap_extent * k_extent;
    ft_uoff file_length, block_size_bitmask = ret_block_size_bitmask;
    ft_u32 i, extent_n = 0;
    int err;

    do {
        if ((err = ff_posix_size(fd, & file_length)) || file_length == 0)
            break;

        /*
         * first pass: call ioctl() and ask how many extents are needed
         *
         * second and further passes: allocate enough extents and call ioctl()
         * with progressively larger buffers until we retrieve all extents
         */
        while ((err = ff_linux_fiemap_ioctl(fd, file_length, extent_n, & k_map)) == 0) {

            ft_u32 ret_extent_n = k_map->fm_mapped_extents;
            
        if (ret_extent_n != 0 && extent_n != 0 && k_map->fm_extent_count != 0) {
                k_extent = k_map->fm_extents;
                if (k_extent[ret_extent_n - 1].fe_flags & FIEMAP_EXTENT_LAST) {
                    /* ok, we really got all the extents */
                    break;
                }
            }
            /*
             * no FIEMAP_EXTENT_LAST found, we did not get all the extents :(
             * 
             * enlarge the array and try again.
             * if extent_n == 0 (first pass), use kernel-suggested length k_map->fm_mapped_extents
             * otherwise increase extent_n exponentially.
             */
            free(k_map);
            k_map = NULL;

            if (extent_n == 0) {
                if ((extent_n = ret_extent_n) <= 1024)
                    extent_n = 1024;
            } else if (ret_extent_n < extent_n) {
                ff_log(FC_WARN, 0, "ioctl(%d, FS_IOC_FIEMAP) is refusing to return more than %"FT_ULL" extents in a single call, falling back on ioctl(FIBMAP) ...", fd, (ft_ull) ret_extent_n);
                /* mark the error as reported, WARN is quite a severe level */
                err = -ENOSYS; /* ioctl(FS_IOC_FIEMAP) not working as expected... */
                break;
            } else if (extent_n <= ((ft_u32)-1) >> 1)
                extent_n <<= 1;
            else if (extent_n < (ft_u32)-1)
                extent_n = (ft_u32)-1;
            else {
                ff_log(FC_DEBUG, 0, "tried ioctl(%d, FS_IOC_FIEMAP) with [MAX_UINT32_T-1] extents but it was not enough, falling back on ioctl(FIBMAP) ...", fd);
                /* do not mark the error as reported, this is just a DEBUG message */
                err = ENOMEM; /* Out of memory */
                break;
            }
            /* keep trying */
        }
        if (err)
            break;

        extent_n = k_map->fm_mapped_extents;
        k_extent = k_map->fm_extents;

        /*
         * perform a first loop, checking for unsupported extents
         * and computing an effective block size
         */
        for (i = 0; i < extent_n; i++) {
            ft_u32 flag = k_extent[i].fe_flags & (FIEMAP_EXTENT_UNKNOWN | FIEMAP_EXTENT_ENCODED);
            
            if (flag) {
                ff_log(FC_DEBUG, 0, "ioctl(%d, FIEMAP, extents[%"FT_ULL"]) returned unsupported %s%s%s extents, falling back on ioctl(FIBMAP) ...",
                       fd, (ft_ull)extent_n,
                       (flag & FIEMAP_EXTENT_UNKNOWN ? "UNKNOWN" : ""),
                       (flag == (FIEMAP_EXTENT_UNKNOWN|FIEMAP_EXTENT_ENCODED) ? " + " : ""),
                       (flag & FIEMAP_EXTENT_ENCODED ? "ENCODED" : "")
                );
                /* do not mark the error as reported, this is just a DEBUG message */
                err = ENOSYS;
                break;
            }
            /* keep track of bits used by all physical, logical and lengths.
             * needed to check against block size */
            block_size_bitmask |= k_extent[i].fe_physical | k_extent[i].fe_logical | k_extent[i].fe_length;
        }
        if (err != 0)
            break;
        ret_list.reserve(ret_list.size() + extent_n);

        /* ok, no strange extents: we can now add them to ret_list */
        for (i = 0; i < extent_n; i++) {
            ret_list.append((ft_uoff) k_extent[i].fe_physical,
                            (ft_uoff) k_extent[i].fe_logical,
                            (ft_uoff) k_extent[i].fe_length,
                            (k_extent[i].fe_flags & FIEMAP_EXTENT_UNWRITTEN) ? FC_EXTENT_ZEROED : FC_DEFAULT_USER_DATA);
        }
    } while (0);

    if (k_map != NULL)
        free(k_map);

    if (err == 0) {
        ff_log(FC_DEBUG, 0, "ioctl(%d, FS_IOC_FIEMAP, extents[%"FT_ULL"]) succeeded", fd, (ft_ull)extent_n);
        ret_block_size_bitmask = block_size_bitmask;
    }

    return err;
#else
    return ENOSYS;
#endif /* FS_IOC_FIEMAP */
}



/**
 * retrieves file blocks allocation map (extents) for specified file descriptor
 * and appends them to ret_vector (with user_data = FC_DEFAULT_USER_DATA)
 * in case of failure returns errno-compatible error code, and ret_vector contents will be UNDEFINED.
 *
 * implementation: calls ioctl(FS_IOC_FIEMAP) and if it fails, tries with ioctl(FIBMAP)
 */
int ff_read_extents_posix(int fd, ft_uoff dev_length, fr_vector<ft_uoff> & ret_list, ft_uoff & ret_block_size_bitmask)
{
    int err;
    do {
        err = ff_linux_fiemap(fd, ret_list, ret_block_size_bitmask);
        if (err != 0) {
            int err2 = ff_posix_fibmap(fd, dev_length, ret_list, ret_block_size_bitmask);
            if (err2 != 0) {
                if (!ff_log_is_reported(err))
                    err = ff_log(FC_ERROR, err,  "%s", "failed to list file blocks with ioctl(FS_IOC_FIEMAP)");
                if (!ff_log_is_reported(err2))
                    err2 = ff_log(FC_ERROR, err2, "%s", "failed to list file blocks with ioctl(FIBMAP)");
            }
            err = err2;
        }
    } while (0);

    return err;
}

FT_IO_NAMESPACE_END
