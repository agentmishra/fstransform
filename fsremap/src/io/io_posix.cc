/*
 * fstransform - transform a file-system to another file-system type,
 *               preserving its contents and without the need for a backup
 *
 * Copyright (C) 2011-2012 Massimiliano Ghilardi
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * io/io_posix.cc
 *
 *  Created on: Feb 28, 2011
 *      Author: max
 */

#include "../first.hh"

#if defined(FT_HAVE_ERRNO_H)
# include <errno.h>        // for errno
#elif defined(FT_HAVE_CERRNO)
# include <cerrno>         // for errno
#endif
#if defined(FT_HAVE_STDIO_H)
# include <stdio.h>        // for remove()
#elif defined(FT_HAVE_CSTDIO)
# include <cstdio>         // for remove()
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

#ifdef FT_HAVE_SYS_TYPES_H
# include <sys/types.h>    // for open()
#endif
#ifdef FT_HAVE_SYS_STAT_H
# include <sys/stat.h>     //  "    "
#endif
#ifdef FT_HAVE_FCNTL_H
# include <fcntl.h>        //  "    "   , fallocate()
#endif
#ifdef FT_HAVE_UNISTD_H
# include <unistd.h>       // for close()
#endif
#ifdef FT_HAVE_SYS_MMAN_H
# include <sys/mman.h>     // for mmap(), munmap()
#endif


#include "../log.hh"      // for ff_log()
#include "../misc.hh"     // for ff_max2(), ff_min2()

#include "../ui/ui.hh"    // for fr_ui

#include "extent_posix.hh" // for ff_read_extents_posix()
#include "util_posix.hh"   // for ff_posix_*() misc functions
#include "io_posix.hh"     // for fr_io_posix


FT_IO_NAMESPACE_BEGIN

#if defined(MAP_ANONYMOUS)
#  define FC_MAP_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#  define FC_MAP_ANONYMOUS MAP_ANON
#else
#  error both MAP_ANONYMOUS and MAP_ANON are missing, cannot compile io_posix.cc
#endif


/** default constructor */
fr_io_posix::fr_io_posix(fr_persist & persist)
: super_type(persist), storage_mmap(MAP_FAILED), buffer_mmap(MAP_FAILED),
  storage_mmap_size(0), buffer_mmap_size(0), this_dev_blkdev(0)
{
    /* mark fd[] as invalid: they are not open yet */
    for (ft_size i = 0; i < FC_ALL_FILE_COUNT; i++)
        fd[i] = -1;

    /* tell superclass that we will invoke ui methods by ourselves */
    delegate_ui(true);
}

/** destructor. calls close() */
fr_io_posix::~fr_io_posix()
{
    close();
}

/** return true if a single descriptor/stream is open */
bool fr_io_posix::is_open0(ft_size i) const
{
    return fd[i] >= 0;
}

/** close a single descriptor/stream */
void fr_io_posix::close0(ft_size i)
{
    if (i < FC_ALL_FILE_COUNT && fd[i] >= 0) {
        if (::close(fd[i]) != 0)
            ff_log(FC_WARN, errno, "closing %s file descriptor [%d] failed", label[i], fd[i]);
        fd[i] = -1;
    }
}

/** return true if this fr_io_posix is currently (and correctly) open */
bool fr_io_posix::is_open() const
{
    return dev_length() != 0 && is_open0(FC_DEVICE);
}

static char const * fr_io_posix_force_msg(bool force) {
    if (force)
        return ", continuing AT YOUR OWN RISK due to '-f'";
    else
        return ", re-run with option '-f' if you want to continue anyway (AT YOUR OWN RISK)";
};

/** open DEVICE */
int fr_io_posix::open_dev(const char * path)
{
    enum { i = FC_DEVICE };
    ft_uoff dev_len;
    ft_dev dev_blk;
    int err = open_dev0(path, & fd[i], & dev_blk, & dev_len);
    if (err != 0)
        return err;

    /* remember device length */
    dev_length(dev_len);
    /* also remember device path */
    dev_path(path);
    /* also remember device major/minor numbers */
    dev_blkdev(dev_blk);

    double pretty_len;
    const char * pretty_label = ff_pretty_size(dev_len, & pretty_len);
    ff_log(FC_INFO, 0, "%s length is %.2f %sbytes", label[i], pretty_len, pretty_label);

    return err;
}

/** actually open DEVICE */
int fr_io_posix::open_dev0(const char * path, int * ret_fd, ft_dev * ret_dev, ft_uoff * ret_len)
{
    enum { i = FC_DEVICE };
    const bool force = force_run();
    const char * force_msg = fr_io_posix_force_msg(force);
    int err = 0, dev_fd;
    do {
        dev_fd = ::open(path, O_RDWR);
        if (dev_fd < 0) {
            err = ff_log(FC_ERROR, errno, "error opening %s '%s'", label[i], path);
            break;
        }

        /* for DEVICE, we want to know its dev_t */
        err = ff_posix_blkdev_dev(dev_fd, ret_dev);
        if (err != 0) {
            err = ff_log((force ? FC_WARN : FC_ERROR), err, "%sfailed %s fstat('%s')%s",
                         (force ? "WARNING: " : ""), label[i], path, force_msg);
            if (force)
                err = 0;
            else
                break;
        }
        /* we also want to know its length */
        if ((err = ff_posix_blkdev_size(dev_fd, ret_len)) != 0) {
            err = ff_log(FC_ERROR, err, "error in %s ioctl('%s', BLKGETSIZE64)", label[i], path);
            break;
        }

    } while (0);

    * ret_fd = dev_fd;

    return err;
}




/** open LOOP-FILE or ZERO-FILE */
int fr_io_posix::open_file(ft_size i, const char * path)
{
    const bool force = force_run();
    const char * force_msg = fr_io_posix_force_msg(force);
    int err = 0;
    ft_dev dev_dev = dev_blkdev();
    bool readwrite = true;
    do {
        if (path == NULL && i == FC_ZERO_FILE) {
            /* zero-file is optional */
            return err;
        }
        /* first, try to open everything as read-write */
        fd[i] = ::open(path, O_RDWR);
        if (fd[i] < 0) {
            readwrite = false;
            /* then, retry to open LOOP-FILE and ZERO-FILE as read-only */
            fd[i] = ::open(path, O_RDONLY);
            if (fd[i] < 0) {
                err = ff_log(FC_ERROR, errno, "error opening %s '%s'", label[i], path);
                break;
            }
        }

        /* for LOOP-FILE and ZERO-FILE, we want to know the dev_t of the device they are stored into */
        ft_dev file_dev;
        err = ff_posix_dev(fd[i], & file_dev);
        if (err != 0) {
            err = ff_log((force ? FC_WARN : FC_ERROR), err, "failed %s fstat('%s')%s",
                         label[i], path, force_msg);
            if (force)
                err = 0;
            else
                break;
        }
        /* for LOOP-FILE and ZERO-FILE, we also check that they are actually contained in DEVICE */
        if (file_dev != dev_dev) {
            ff_log((force ? FC_WARN : FC_ERROR), 0, "'%s' is device 0x%04x, but %s '%s' is contained in device 0x%04x%s",
                   dev_path(), (unsigned)dev_dev, label[i], path, (unsigned)file_dev, force_msg);
            if (!force) {
                err = -EINVAL;
                break;
            }
        }
        if (readwrite) {
            /*
             * check only now if open(O_RDWR) succeeded:
             * before telling the user that DEVICE is not mounted read-only,
             * we need to know that LOOP-FILE and ZERO-FILE are actually inside DEVICE,
             * otherwise the message below will mislead the user
             */
            close0(i);
            ff_log(FC_ERROR, 0, "%s '%s' can be opened read-write, it means %s '%s' is not mounted read-only as it should",
                   label[i], path, label[FC_DEVICE], dev_path());
            err = -EINVAL;
            break;
        }


        ft_uoff len, dev_len = dev_length();
        /* for LOOP-FILE and ZERO-FILE, we check their length */
        if ((err = ff_posix_size(fd[i], & len)) != 0) {
            err = ff_log((force ? FC_WARN : FC_ERROR), err, "failed %s fstat('%s')%s",
                         label[i], path, force_msg);
            if (force)
                err = 0;
            else
                break;
        }
        if (i == FC_LOOP_FILE) {
            /* remember LOOP-FILE length */
            loop_file_length(len);

            /*
             * in some cases, it can happen that device has a last odd-sized block,
             * for example a device 1GB+1kB long with 4kB block size,
             * and at least on Linux it's not easy to write to the last odd-sized block:
             * the filesystem driver usually prevents it.
             *
             * So we play it safe and truncate device length to a multiple of its block size.
             * Funnily enough, a good way to get device block size is to stat() a file inside it
             */
            ft_uoff block_size;
            if (ff_posix_blocksize(fd[i], & block_size) != 0) {
                ff_log(FC_WARN, errno, "%s fstat('%s') failed, assuming %s block size is at most 4 kilobytes",
                                       label[i], path, label[FC_DEVICE]);
                block_size = 4096;
            } else if (block_size < 512) {
                ff_log(FC_WARN, errno, "%s fstat('%s') reported suspiciously small block size (%" FT_ULL " bytes) for %s, rounding block size to 512 bytes",
                                       label[i], path, (ft_ull) block_size, label[FC_DEVICE]);
                block_size = 512;
            }

            /** remember rounded device length */
            ft_uoff dev_len_rounded = dev_len - dev_len % block_size;
            dev_length(dev_len_rounded);

            if (len > dev_len_rounded) {
                ff_log(FC_ERROR, 0, "cannot start %sremapping: %s '%s' length (%" FT_ULL " bytes) exceeds %s '%s' size (%" FT_ULL " bytes)",
                        (simulate_run() ? "(simulated) " : ""),
                        label[i], path, (ft_ull)len, label[FC_DEVICE], dev_path(), (ft_ull)dev_len_rounded);
                if (dev_len_rounded != dev_len) {
                    ff_log(FC_ERROR, 0, "    Note: %s size is actually %" FT_ULL " bytes, "
                            "but fsremap needs to round it down to a multiple of file-system block size (%" FT_ULL " bytes)",
                            label[FC_DEVICE], (ft_ull)dev_len, (ft_ull)block_size);
                    ff_log(FC_ERROR, 0, "    so the usable %s size is %" FT_ULL " bytes",
                            label[FC_DEVICE], (ft_ull)dev_len_rounded);
                }
                ff_log(FC_ERROR, 0, "Exiting, please shrink %s to %" FT_ULL " bytes or less before running fsremap again.",
                        label[i], (ft_ull) dev_len_rounded);
                ff_log(FC_ERROR, 0, "    (if you are using fstransform - i.e. if you did not manually run fsremap - "
                        "then this is a BUG in fstransform, please report it)");
                err = -EFBIG;
                break;
            } else if (len < dev_len_rounded) {
                ff_log(FC_INFO, 0, "%s '%s' is shorter than %s, remapping will also shrink file-system",
                       label[i], path, label[FC_DEVICE]);
            }
        }
    } while (0);

    return err;
}


/** check for consistency and open DEVICE, LOOP-FILE and ZERO-FILE */
int fr_io_posix::open(const fr_args & args)
{
    if (is_open()) {
        // already open!
        ff_log(FC_ERROR, 0, "unexpected call, I/O is already open");
        return -EISCONN;
    }
    int err = super_type::open(args);
    if (err != 0)
        return err;

#ifdef FT_HAVE_GETUID
    if (getuid() != 0)
        ff_log(FC_WARN, 0, "not running as root! expect '%s' errors", strerror(EPERM));
#endif

    char const* const* path = args.io_args;
    do {
        ft_size i = FC_DEVICE;
        if ((err = open_dev(path[i])) != 0)
            break;

        if (!is_replaying())
            for (i = FC_DEVICE + 1; i < FC_FILE_COUNT; i++)
                if ((err = open_file(i, path[i])) != 0)
                    break;

    } while (0);

    if (err != 0)
        close();

    return err;
}



/** close this I/O, including file descriptors to DEVICE, LOOP-FILE, ZERO-FILE and SECONDARY-STORAGE */
void fr_io_posix::close()
{
    for (ft_size i = 0; i < FC_FILE_COUNT; i++)
        close0(i);

    close_storage();

    super_type::close();
}



/** return true if this I/O has open descriptors/streams to LOOP-FILE and FREE-SPACE */
bool fr_io_posix::is_open_extents() const
{
    /* FREE-SPACE is optional, do not check if it's open */
    return dev_length() != 0 && is_open0(FC_LOOP_FILE);
}

/**
 * retrieve LOOP-FILE extents, FREE-SPACE extents and any additional extents to be ZEROED
 * and insert them into the vectors loop_file_extents, free_space_extents and to_zero_extents
 * the vectors will be ordered by extent ->logical (for to_zero_extents, ->physical and ->logical will be the same).
 *
 * return 0 for success, else error (and vectors contents will be UNDEFINED).
 *
 * if success, also returns in ret_effective_block_size_log2 the log2()
 * of device effective block size.
 * the device effective block size is defined as follows:
 * it is the largest power of 2 that exactly divides all physical,
 * logical and lengths in all returned extents (both for LOOP-FILE
 * and for FREE-SPACE) and that also exactly exactly divides device length.
 *
 * must be overridden by sub-classes.
 *
 * the trick fr_io_posix uses to implement this method
 * is to fill the device's free space with a ZERO-FILE,
 * and actually retrieve the extents used by ZERO-FILE.
 */
int fr_io_posix::read_extents(fr_vector<ft_uoff> & loop_file_extents,
                              fr_vector<ft_uoff> & free_space_extents,
                              fr_vector<ft_uoff> & to_zero_extents,
                              ft_uoff & ret_block_size_bitmask)
{
    ft_uoff block_size_bitmask = ret_block_size_bitmask;
    int err = 0;
    do {
        if (!is_open_extents()) {
            ff_log(FC_ERROR, 0, "unexpected call to io_posix::read_extents(), I/O is not open");
            err = -ENOTCONN; // not open!
            break;
        }

        if ((err = read_extents_loop_file(loop_file_extents, to_zero_extents, block_size_bitmask)) != 0)
            break;

        if ((err = read_extents_free_space(loop_file_extents, free_space_extents, to_zero_extents, block_size_bitmask)) != 0)
            break;

    } while (0);

    if (err == 0)
        ret_block_size_bitmask = block_size_bitmask;

    return err;
}


/**
 * retrieve LOOP-FILE extents and any additional extents to be ZEROED
 * and insert them into the vectors loop_file_extents, and to_zero_extents
 * the vectors will be ordered by extent ->logical (for to_zero_extents, ->physical and ->logical will be the same).
 *
 * return 0 for success, else error (and vectors contents will be UNDEFINED).
 *
 * if success, also update the parameter ret_effective_block_size_log2 to be the log2()
 * of device effective block size (see read_extents() for detailed meaning of this parameter)
 */
int fr_io_posix::read_extents_loop_file(fr_vector<ft_uoff> & loop_file_extents,
                                        fr_vector<ft_uoff> & FT_ARG_UNUSED(to_zero_extents),
                                        ft_uoff & ret_block_size_bitmask)
{
    ft_uoff block_size_bitmask = ret_block_size_bitmask;
    int err = 0;
    do {
        if (!is_open_extents()) {
            ff_log(FC_ERROR, 0, "unexpected call to io_posix::read_extents_loop_file(), I/O is not open");
            err = -ENOTCONN; // not open!
            break;
        }

        ft_uoff dev_len = dev_length();

        /* ff_read_extents_posix() appends into fr_vector<T>, does NOT overwrite it */
        if ((err = ff_read_extents_posix(fd[FC_LOOP_FILE], dev_len, loop_file_extents, block_size_bitmask)) != 0)
            break;

    } while (0);

    if (err == 0)
        ret_block_size_bitmask = block_size_bitmask;

    return err;
}

/**
 * retrieve FREE-SPACE extents and any additional extents to be ZEROED
 * and insert them into the vectors free_space_extents, and to_zero_extents
 * the vectors will be ordered by extent ->logical (for to_zero_extents, ->physical and ->logical will be the same).
 *
 * return 0 for success, else error (and vectors contents will be UNDEFINED).
 *
 * if success, also update the parameter ret_effective_block_size_log2 to be the log2()
 * of device effective block size (see read_extents() for detailed meaning of this parameter)
 */
int fr_io_posix::read_extents_free_space(const fr_vector<ft_uoff> & loop_file_extents,
                                         fr_vector<ft_uoff> & free_space_extents,
                                         fr_vector<ft_uoff> & FT_ARG_UNUSED(to_zero_extents),
                                         ft_uoff & ret_block_size_bitmask)
{
    ft_uoff block_size_bitmask = ret_block_size_bitmask;
    int err = 0;
    do {
        if (!is_open_extents()) {
            ff_log(FC_ERROR, 0, "unexpected call to io_posix::read_extents_free_space(), I/O is not open");
            err = -ENOTCONN; // not open!
            break;
        }

        ft_uoff dev_len = dev_length();

        if (fd[FC_ZERO_FILE] >= 0) {
            if ((err = ff_read_extents_posix(fd[FC_ZERO_FILE], dev_len, free_space_extents, block_size_bitmask)) != 0)
                break;
        } else {
            block_size_bitmask |= dev_len;
            /*
             * ZERO-FILE is optional.
             * if not specified, prepare for an irreversible remapping that does not preserve DEVICE:
             * consider *ALL* extents outside LOOP-FILE as free
             */
            free_space_extents = loop_file_extents;
            free_space_extents.sort_by_physical();

            fr_map<ft_uoff> free_map;
            free_map.complement0_physical_shift(free_space_extents, 0, dev_len);

            free_space_extents.clear();

            fr_map<ft_uoff>::const_iterator iter, end = free_map.end();
            for (iter = free_map.begin(); iter != end; ++iter)
                free_space_extents.append(*iter);
        }

    } while (0);

    if (err == 0)
        ret_block_size_bitmask = block_size_bitmask;

    return err;
}


/**
 * close the file descriptors for LOOP-FILE and ZERO-FILE
 */
void fr_io_posix::close_extents()
{
    close0(FC_ZERO_FILE);
    close0(FC_LOOP_FILE);
}

/**
 * close and munmap() SECONDARY-STORAGE. does NOT remove it from file system!
 * called by close() and by work<T>::close_storage()
 */
int fr_io_posix::close_storage()
{
    int err = 0;
    enum { i = FC_PRIMARY_STORAGE, j = FC_SECONDARY_STORAGE };
    if (storage_mmap != MAP_FAILED) {
        if (munmap(storage_mmap, storage_mmap_size) == 0) {
            storage_mmap = MAP_FAILED;
            storage_mmap_size = 0;
        } else {
            bool flag_i = !primary_storage().empty();
            bool flag_j = secondary_storage().length() != 0;
            err = ff_log(FC_WARN, errno, "warning: %s%s%s munmap() failed",
                         (flag_i ? label[i] : ""),
                         (flag_i && flag_j ? " and " : ""),
                         (flag_j ? label[j] : "")
            );
        }
    }
    if (err == 0 && buffer_mmap != MAP_FAILED) {
        if (munmap(buffer_mmap, buffer_mmap_size) == 0) {
            buffer_mmap = MAP_FAILED;
            buffer_mmap_size = 0;
        } else {
            err = ff_log(FC_WARN, errno, "warning: memory buffer munmap() failed");
        }
    }
    if (err == 0) {
        close0(i);
        close0(j);
    }
    return err;
}

/**
 * create and open SECONDARY-STORAGE job.job_dir() + '.storage',
 * fill it with 'secondary_len' bytes of zeros and mmap() it.
 *
 * then mmap() this->primary_storage extents.
 * finally setup a virtual storage composed by 'primary_storage' extents inside DEVICE, plus secondary-storage extents.
 *
 * return 0 if success, else error
 */
int fr_io_posix::create_storage(ft_size secondary_size, ft_size mem_buffer_size)
{
    /*
     * how to get a contiguous mmapped() memory for all the extents in primary_storage and secondary_storage:
     *
     * mmap(MAP_ANON/MAP_ANONYMOUS) the total storage size (sum(primary_storage->length)+secondary_len)
     * then incrementally replace parts of it with munmap() followed by mmap(MAP_FIXED) of each storage extent
     */
    enum { i = FC_PRIMARY_STORAGE, j = FC_SECONDARY_STORAGE };

    if (storage_mmap != MAP_FAILED || is_open0(j)) {
        // already initialized!
        ff_log(FC_ERROR, 0, "unexpected call to create_storage(), %s is already initialized",
               storage_mmap != MAP_FAILED ? label[i] : label[j]);
        // return error as already reported
        return -EISCONN;
    }

    /**
     * recompute primary_len... we could receive it from caller, but it's redundant
     * and in any case we will still need to iterate on primary_storage to mmap() it
     */
    ft_uoff primary_len = 0;
    fr_vector<ft_uoff>::iterator begin = primary_storage().begin(), iter, end = primary_storage().end();
    for (iter = begin; iter != end; ++iter) {
        primary_len += iter->second.length;
    }

    double pretty_len;
    const char * pretty_label;
    int err = 0;
    do {
        const ft_size mmap_size = (ft_size) primary_len + secondary_size;
        if (primary_len > (ft_size)-1 - secondary_size) {
            err = ff_log(FC_FATAL, EOVERFLOW, "internal error, %s + %s total length = %" FT_ULL " is larger than addressable memory",
                         label[i], label[j], (ft_ull) primary_len + secondary_size);
            break;
        }
        /*
         * mmap() total length as PROT_NONE, FC_MAP_ANONYMOUS.
         * used to reserve a large enough contiguous memory area
         * to mmap() PRIMARY STORAGE and SECONDARY STORAGE
         */
        storage_mmap = mmap(NULL, mmap_size, PROT_NONE, MAP_PRIVATE|FC_MAP_ANONYMOUS, -1, 0);
        if (storage_mmap == MAP_FAILED) {
            err = ff_log(FC_ERROR, errno, "%s: error preemptively reserving contiguous RAM: mmap(length = %" FT_ULL ", PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1) failed",
                    label[FC_STORAGE], (ft_ull) mmap_size);
            break;
        } else
            ff_log(FC_DEBUG, 0, "%s: preemptively reserved contiguous RAM,"
                    " mmap(length = %" FT_ULL ", PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1) = ok",
                    label[FC_STORAGE], (ft_ull) mmap_size);
        storage_mmap_size = mmap_size;
        /*
         * mmap() another area, mem_buffer_size bytes long, as PROT_READ|PROT_WRITE, FC_MAP_ANONYMOUS.
         * used as memory buffer during DEV2DEV copies
         */
        buffer_mmap = mmap(NULL, mem_buffer_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|FC_MAP_ANONYMOUS, -1, 0);
        if (buffer_mmap == MAP_FAILED) {
            err = ff_log(FC_ERROR, errno, "%s: error allocating memory buffer: mmap(length = %" FT_ULL ", PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1) failed",
                    label[FC_STORAGE], (ft_ull) mem_buffer_size);
            break;
        }
        /*
         * we could mlock(buffer_mmap), but it's probably excessive
         * as it constraints too much the kernel in deciding the memory to swap to disk.
         *
         * instead, we opt for a simple memset(), which forces the kernel to actually allocate
         * the RAM for us (we do not want memory overcommit errors later on),
         * but still let the kernel decide what to swap to disk
         */
        memset(buffer_mmap, '\0', buffer_mmap_size = mem_buffer_size);

        pretty_len = 0.0;
        pretty_label = ff_pretty_size(buffer_mmap_size, & pretty_len);

        ff_log(FC_NOTICE, 0, "allocated %.2f %sbytes RAM as memory buffer", pretty_len, pretty_label);




        if (secondary_size != 0) {
            if ((err = create_secondary_storage(secondary_size)) != 0)
                break;
        } else
            ff_log(FC_INFO, 0, "not creating %s, %s is large enough", label[j], label[i]);

        /* now incrementally replace storage_mmap with actually mmapped() storage extents */
        begin = primary_storage().begin();
        end = primary_storage().end();
        ft_size mem_offset = 0;
        for (iter = begin; err == 0 && iter != end; ++iter)
            err = replace_storage_mmap(fd[FC_DEVICE], label[i], *iter, iter-begin, mem_offset);
        if (err != 0)
            break;

        if (secondary_size != 0) {
            if ((err = replace_storage_mmap(fd[j], label[j], secondary_storage(), 0, mem_offset)) != 0)
                break;
        }
        if (mem_offset != storage_mmap_size) {
            ff_log(FC_FATAL, 0, "internal error, mapped %s extents in RAM used %" FT_ULL " bytes instead of expected %" FT_ULL " bytes",
                    label[FC_STORAGE], (ft_ull) mem_offset, (ft_ull) storage_mmap_size);
            err = EINVAL;
        }
    } while (0);

    if (err == 0) {
        pretty_len = 0.0;
        pretty_label = ff_pretty_size(storage_mmap_size, & pretty_len);

        ff_log(FC_NOTICE, 0, "%s%s%s is %.2f %sbytes, initialized and mmapped() to contiguous RAM",
                (primary_len != 0 ? label[i] : ""),
                (primary_len != 0 && secondary_size != 0 ? " + " : ""),
                (secondary_size != 0 ? label[j] : ""),
                pretty_len, pretty_label);
    } else
        close_storage();

    return err;
}

/**
 * replace a part of the mmapped() storage_mmap area with specified storage_extent,
 * and store mmapped() address into storage_extent.user_data().
 * return 0 if success, else error
 *
 * note: fd shoud be this->fd[FC_DEVICE] for primary storage,
 * or this->fd[FC_SECONDARY_STORAGE] for secondary storage
 */
int fr_io_posix::replace_storage_mmap(int fd, const char * label_i,
        fr_extent<ft_uoff> & storage_extent, ft_size extent_index,
        ft_size & ret_mem_offset)
{
    ft_size len = (ft_size) storage_extent.length();
    ft_size mem_start = ret_mem_offset, mem_end = mem_start + len;
    int err = 0;
    do {
        if (mem_start >= storage_mmap_size || mem_end > storage_mmap_size) {
            ff_log(FC_FATAL, 0, "internal error mapping %s extent #%" FT_ULL " in RAM!"
                    " extent (%" FT_ULL ", length = %" FT_ULL ") overflows total %s length = %" FT_ULL ,
                    label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len,
                    label[FC_STORAGE], (ft_ull) storage_mmap_size);
            /* mark error as reported */
            err = -EINVAL;
            break;
        }
        void * addr_old = (char *) storage_mmap + mem_start;
        if (munmap(addr_old, len) != 0) {
            err = ff_log(FC_ERROR, errno, "error mapping %s extent #%" FT_ULL " in RAM,"
                    " munmap(address + %" FT_ULL ", length = %" FT_ULL ") failed",
                    label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len);
            break;
        }
        void * addr_new = mmap(addr_old, len, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, storage_extent.physical());
        if (addr_new == MAP_FAILED) {
            err = ff_log(FC_ERROR, errno, "error mapping %s extent #%" FT_ULL " in RAM,"
                    " mmap(address + %" FT_ULL ", length = %" FT_ULL ", MAP_FIXED) failed",
                    label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len);
            break;
        }
        if (addr_new != addr_old) {
            ff_log(FC_ERROR, 0, "error mapping %s extent #%" FT_ULL " in RAM,"
                    " mmap(address + %" FT_ULL ", length = %" FT_ULL ", MAP_FIXED)"
                   " violated MAP_FIXED and returned a different address",
                   label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len);
            /* mark error as reported */
            err = -EFAULT;
            /** try at least to munmap() this problematic extent */
            if (munmap(addr_new, len) != 0) {
                ff_log(FC_WARN, errno, "weird OS! not only mmap() violated MAP_FIXED, but subsequent munmap() failed too");
            }
            break;
        }
        ff_log(FC_TRACE, 0, "%s extent #%" FT_ULL " mapped in RAM,"
                " mmap(address + %" FT_ULL ", length = %" FT_ULL ", MAP_FIXED) = ok",
                label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len);

#ifdef FT_HAVE_MLOCK
        if (!simulate_run() && mlock(addr_new, len) != 0) {
            ff_log(FC_WARN, errno, "%s extent #%" FT_ULL " mlock(address + %" FT_ULL ", length = %" FT_ULL ") failed",
                   label_i, (ft_ull) extent_index, (ft_ull) mem_start, (ft_ull) len);
        }
#else
#warning mlock() not found on this platform. fsremap will be vulnerable to memory exhaustion from other programs
        static bool warned_no_mlock = false;
        if (!warned_no_mlock) {
            warned_no_mlock = true;
            ff_log(FC_WARN, 0, "fsremap was compiled without support for mlock()");
            ff_log(FC_WARN, 0, "for the safety of your data, please do not start memory-hungry programs while fsremap is running");
        }
#endif
        /**
         * all ok, let's store mmapped() address offset into extent.user_data to remember it,
         * as msync() inside flush() and munmap() inside close_storage() could need it
         */
        storage_extent.user_data() = mem_start;
        /* and remember to update ret_mem_offset */
        ret_mem_offset += len;

    } while (0);
    return err;
}


/**
 * create and open SECONDARY-STORAGE in job.job_dir() + '.storage'
 * and fill it with 'secondary_len' bytes of zeros. do not mmap() it.
 * return 0 if success, else error
 */
int fr_io_posix::create_secondary_storage(ft_size len)
{
    enum { j = FC_SECONDARY_STORAGE };

    ft_string filepath = job_dir();
    filepath += "/storage.bin";
    const char * path = filepath.c_str();
    int err = 0;
    const bool simulated = simulate_run();
    const bool replaying = is_replaying();

    do {
        const ft_off s_len = (ft_off) len;
        if (s_len < 0 || len != (ft_size) s_len) {
            err = ff_log(FC_FATAL, EOVERFLOW, "internal error, %s length = %" FT_ULL " overflows type (off_t)", label[j], (ft_ull) len);
            break;
        }

        double pretty_len = 0.0;
        const char * pretty_label = ff_pretty_size(len, & pretty_len);
        const char * simulated_msg = simulated ? " (simulated)" : "";

        if ((fd[j] = ::open(path, replaying ? O_RDWR : O_RDWR|O_CREAT|O_TRUNC, 0600)) < 0) {
            err = ff_log(FC_ERROR, errno, "error in %s open('%s')", label[j], path);
            if (replaying && err == -ENOENT)
                ff_log(FC_ERROR, 0, "you probably tried to resume a COMPLETED job");
            break;
        }

        if (replaying) {
            ft_uoff actual_len = 0;
            err = ff_posix_size(fd[j], & actual_len);
            if (err != 0) {
                err = ff_log(FC_ERROR, err, "%s: fstat('%s') failed", label[j], path);

            } else if (actual_len != (ft_uoff) len) {
                ff_log(FC_ERROR, 0, "%s: file '%s' is %" FT_ULL " bytes long, expecting %" FT_ULL " bytes instead",
                        label[j], path, (ft_ull) actual_len, (ft_ull) len);
                err = -EINVAL;
            } else
                ff_log(FC_INFO, 0, "%s: opened existing file '%s', is %.2f %sbytes long", label[j],
                        path, pretty_len, pretty_label);
        } else {
            ff_log(FC_INFO, 0, "%s:%s writing %.2f %sbytes to '%s' ...", label[j], simulated_msg,
                    pretty_len, pretty_label, path);
            if (simulated) {
                if ((err = ff_posix_lseek(fd[j], len - 1)) != 0) {
                    err = ff_log(FC_ERROR, errno, "error in %s lseek('%s', offset = %" FT_ULL " - 1)", label[j], path, (ft_ull)len);
                    break;
                }
                char zero = '\0';
                if ((err = ff_posix_write(fd[j], & zero, 1)) != 0) {
                    err = ff_log(FC_ERROR, errno, "error in %s write('%s', '\\0', length = 1)", label[j], path);
                    break;
                }
            } else {
                ft_string err_msg = ft_string("error in ") + label[j] + "write('" + path + "')";

                err = ff_posix_fallocate(fd[j], s_len, err_msg);
            }
            if (err == 0)
                ff_log(FC_INFO, 0, "%s:%s file created", label[j], simulated_msg);
        }
        if (err != 0)
            break;

        /* remember secondary_storage details */
        fr_extent<ft_uoff> & extent = secondary_storage();
        extent.physical() = extent.logical() = 0;
        extent.length() = len;


    } while (0);

    if (err != 0) {
        const bool need_remove = !replaying && is_open0(j);
        close0(fd[j]);
        if (need_remove && remove(path) != 0 && errno != ENOENT)
            ff_log(FC_WARN, errno, "removing %s file '%s' failed", label[j], path);
    }
    return err;
}

/**
 * called to remove SECONDARY-STORAGE file job.job_dir() + '/storage.bin' from file system
 * if execution is completed successfully
 * return 0 if success, else error
 */
int fr_io_posix::remove_storage_after_success()
{
    enum { j = FC_SECONDARY_STORAGE };

    ft_string filepath = job_dir();
    filepath += "/storage.bin";
    const char * path = filepath.c_str();

    if (remove(path) != 0 && errno != ENOENT)
        ff_log(FC_WARN, errno, "removing %s file '%s' failed", label[j], path);
    return 0;
}

/** call umount(8) on dev_path() */
int fr_io_posix::umount_dev()
{
    const char * cmd = cmd_umount(), * dev = dev_path();

    std::vector<const char *> args;

    if (cmd == NULL)
        // posix standard name for umount(8)
        cmd = "/bin/umount";

    args.push_back(cmd);
    // only one argument: device path
    args.push_back(dev);
    args.push_back(NULL); // needed by ff_posix_exec() as end-of-arguments marker

    ff_log(FC_INFO, 0, "unmounting %s '%s'... command: %s %s", label[FC_DEVICE], dev, cmd, dev);

    int err = ff_posix_exec(args[0], & args[0]);

    if (err == 0)
        ff_log(FC_NOTICE, 0, "successfully unmounted %s '%s'", label[FC_DEVICE], dev);

    (void) sync(); // sync() returns void

    return err;
}



/**
 * called once by work<T>::relocate() immediately before starting the remapping phase.
 *
 * checks that last device block to be written is actually writable.
 * Reason: if linux filesystems are smaller than the device, they often limit the writable blocks in the device to their length.
 */
int fr_io_posix::check_last_block()
{
    ft_uoff loop_file_len = loop_file_length();
    int err = 0;
    if (loop_file_len-- == 0)
        return err;

    const char * label_dev   = label[FC_DEVICE];
    int fd_dev = fd[FC_DEVICE];
    char ch = '\0';
    bool simulated = simulate_run();

    for (int i = 0; err == 0 && i < 2; i++) {
        if ((err = ff_posix_lseek(fd_dev, loop_file_len)) != 0) {
            err = ff_log(FC_ERROR, err, "I/O error in %s lseek(fd = %d, offset = %" FT_ULL ", SEEK_SET)",
                         label_dev, fd_dev, (ft_ull)loop_file_len);
            break;
        }

        if (i == 0) {
            if ((err = ff_posix_read(fd_dev, &ch, 1)) != 0)
                err = ff_log(FC_ERROR, err, "I/O error in %s read(fd = %d, offset = %" FT_ULL ", len = 1)",
                        label_dev, fd_dev, (ft_ull)loop_file_len);

        } else if (!simulated) {
            if ((err = ff_posix_write(fd_dev, &ch, 1)) != 0)
                err = ff_log(FC_ERROR, err, "last position to be written into %s (offset = %" FT_ULL ") is NOT writable",
                        label_dev, (ft_ull)loop_file_len);
        }
    }
    return err;
}

/**
 * actually copy a list of fragments from DEVICE to STORAGE, or from STORAGE or DEVICE, or from DEVICE to DEVICE.
 * note: parameters are in bytes!
 * return 0 if success, else error.
 *
 * we expect request_vec to be sorted by ->physical (i.e. ->from_physical)
 */
int fr_io_posix::flush_copy_bytes(fr_dir dir, fr_vector<ft_uoff> & request_vec)
{
    int err = 0;


    switch (dir) {
    case FC_DEV2STORAGE: {
        /* from DEVICE to memory-mapped STORAGE */
        /* sequential disk access: request_vec is supposed to be already sorted by device from_offset, i.e. extent->physical */
        fr_vector<ft_uoff>::const_iterator iter = request_vec.begin(), end = request_vec.end();
        for (; err == 0 && iter != end; ++iter)
            err = flush_copy_bytes(FC_POSIX_DEV2STORAGE, *iter);
        break;
    }
    case FC_STORAGE2DEV: {
        /* from memory-mapped STORAGE to DEVICE */
        /* sequential disk access: request_vec is supposed to be already sorted by device to_offset, i.e. extent->logical */
        fr_vector<ft_uoff>::const_iterator iter = request_vec.begin(), end = request_vec.end();
        for (; err == 0 && iter != end; ++iter)
            err = flush_copy_bytes(FC_POSIX_STORAGE2DEV, *iter);
        break;
    }
    case FC_DEV2DEV: {
        /* from DEVICE to DEVICE, using RAM buffer */
        /* sequential disk access: request_vec is supposed to be sorted by device to_offset, i.e. extent->logical */

        request_vec.sort_by_physical(); /* sort by device from_offset, i.e. extent->physical */

        ft_uoff from_offset, to_offset, length;
        ft_size buf_offset = 0, buf_free = buffer_mmap_size, buf_length;

        ft_size start = 0, i = start, save_i, n = request_vec.size();

        do {
            /* iterate and fill buffer_mmap */
            for (; err == 0 && buf_free != 0 && i < n; ++i) {
                fr_extent<ft_uoff> & extent = request_vec[i];
                if ((length = extent.length()) > (ft_uoff) buf_free)
                    break;
                if ((err = flush_copy_bytes(FC_POSIX_DEV2RAM, extent.physical(), (ft_uoff)(extent.user_data() = buf_offset), length)) != 0)
                    break;
                buf_offset += (ft_size) length;
                buf_free -= (ft_size) length;
            }
            if (err != 0)
                break;

            /* buffer_mmap is now (almost) full. sort buffered data by device to_offset (i.e. extent->logical) and write it to target */
            if ((save_i = i) != start) {
                request_vec.sort_by_logical(request_vec.begin() + start, request_vec.begin() + i);

                for (i = start; err == 0 && i != save_i; ++i) {
                    fr_extent<ft_uoff> & extent = request_vec[i];
                    if ((err = flush_copy_bytes(FC_POSIX_RAM2DEV, (ft_uoff) extent.user_data(), extent.logical(), extent.length())) != 0)
                        break;
                }
            }

            if (err != 0 || (err = flush_bytes()) != 0)
                break;

            /* buffered data written to target. now there may be one or more extents NOT fitting into buffer_mmap */
            buf_offset = 0, buf_free = buffer_mmap_size;
            for (i = save_i; err == 0 && i != n; ++i) {
                fr_extent<ft_uoff> & extent = request_vec[i];
                if ((length = extent.length()) <= buf_free)
                    break;

                from_offset = extent.physical();
                to_offset = extent.logical();
                while (length != 0) {
                    buf_length = (ft_size) ff_min2<ft_uoff>(length, buf_free);

                    if ((err = flush_copy_bytes(FC_POSIX_DEV2RAM, from_offset, buf_offset, buf_length)) != 0
                        || (err = flush_copy_bytes(FC_POSIX_RAM2DEV, buf_offset, to_offset, buf_length)) != 0
                        || (err = flush_bytes()) != 0)
                        break;

                    length      -= (ft_uoff) buf_length;
                    from_offset += (ft_uoff) buf_length;
                    to_offset   += (ft_uoff) buf_length;
                }
                if (err != 0)
                    break;
            }
        } while (err == 0 && (start = i) != n);
        break;
    }
    default:
        /* from STORAGE to STORAGE */
        err = ff_log(FC_FATAL, ENOSYS, "internal error! unexpected call to io_posix.copy_bytes(), STORAGE to STORAGE copies are not supposed to be used");
        break;
    }
    return err;
}


int fr_io_posix::flush_copy_bytes(fr_dir_posix dir, const fr_extent<ft_uoff> & request)
{
    return flush_copy_bytes(dir, request.physical(), request.logical(), request.length());
}

#undef ENABLE_CHECK_IF_MEM_IS_ZERO

#ifdef ENABLE_CHECK_IF_MEM_IS_ZERO
/** returns true if the specified memory range contains ONLY zeroes. */
static bool fr_io_posix_mem_is_zero(const char * mem_address, ft_size mem_length) {
    for (ft_size i = 0; i < mem_length; i++) {
        if (mem_address[i] != 0)
            return false;
    }
    return true;
}
#endif // ENABLE_CHECK_IF_MEM_IS_ZERO



int fr_io_posix::flush_copy_bytes(fr_dir_posix dir, ft_uoff from_offset, ft_uoff to_offset, ft_uoff length)
{
    const bool use_storage = dir == FC_POSIX_DEV2STORAGE || dir == FC_POSIX_STORAGE2DEV;
    const bool read_dev = dir == FC_POSIX_DEV2STORAGE || dir == FC_POSIX_DEV2RAM;

    const char * label_dev   = label[FC_DEVICE];
    const char * label_other = use_storage ? label[FC_STORAGE] : "RAM";
    const char * label_from = read_dev ? label_dev : label_other;
    const char * label_to = read_dev ? label_other : label_dev;

    const ft_size mmap_size = use_storage ? storage_mmap_size : buffer_mmap_size;

    const ft_uoff dev_offset = read_dev ? from_offset : to_offset;
    const ft_uoff other_offset = read_dev ? to_offset : from_offset;

    /* validate("label", N, ...) also checks if from/to + length overflows (ft_uoff)-1 */
    int err = validate("ft_uoff", (ft_uoff)-1, dir, from_offset, to_offset, length);
    if (err == 0)
        err = validate("ft_size", (ft_uoff)mmap_size, dir, 0, other_offset, length);
    if (err != 0)
        return err;

    const ft_size mem_offset = (ft_size)other_offset;
    const ft_size mem_length = (ft_size)length;

    char * mmap_address = use_storage ? (char *)storage_mmap : (char *)buffer_mmap;
    const int fd = this->fd[FC_DEVICE];
    const bool simulated = simulate_run();

    if (ui() != NULL) {
        if (dir != FC_POSIX_RAM2DEV) {
            fr_from from = dir == FC_POSIX_STORAGE2DEV ? FC_FROM_STORAGE : FC_FROM_DEV;
            ui()->show_io_read(from, from_offset, length);
        }
        if (dir!= FC_POSIX_DEV2RAM) {
            fr_to to = dir == FC_POSIX_DEV2STORAGE ? FC_TO_STORAGE : FC_TO_DEV;
            ui()->show_io_write(to, to_offset, length);
        }
    }
    do {
        if (!simulated) {
            if ((err = ff_posix_lseek(fd, dev_offset)) != 0) {
                err = ff_log(FC_ERROR, err, "I/O error in %s lseek(fd = %d, offset = %" FT_ULL ", SEEK_SET)",
                        label_dev, fd, (ft_ull)dev_offset);
                break;
            }
#define CURRENT_OP_FMT "from %s to %s, %s({fd = %d, offset = %" FT_ULL "}, address + %" FT_ULL ", length = %" FT_ULL ")"
#define CURRENT_OP_ARGS label_from, label_to, (read_dev ? "read" : "write"), fd, (ft_ull)dev_offset, (ft_ull)mem_offset, (ft_ull)mem_length

#ifdef ENABLE_CHECK_IF_MEM_IS_ZERO
# define CHECK_IF_MEM_IS_ZERO \
            do { \
                if (fr_io_posix_mem_is_zero(mmap_address + mem_offset, mem_length)) { \
                    ff_log(FC_WARN, 0, "found an extent full of zeros copying " CURRENT_OP_FMT ". Stopping, press ENTER to continue.", CURRENT_OP_ARGS); \
                    char ch; \
                    (void) ff_posix_read(0, &ch, 1); \
                } \
            } while (0)

#else // !ENABLE_CHECK_IF_MEM_IS_ZERO

# define CHECK_IF_MEM_IS_ZERO do { } while (0)

#endif // ENABLE_CHECK_IF_MEM_IS_ZERO

            if (read_dev) {
                err = ff_posix_read(fd, mmap_address + mem_offset, mem_length);
                if (err == 0) {
                    CHECK_IF_MEM_IS_ZERO;
                }
            } else {
                CHECK_IF_MEM_IS_ZERO;
                err = ff_posix_write(fd, mmap_address + mem_offset, mem_length);
            }
            if (err != 0) {
                err = ff_log(FC_ERROR, err, "I/O error while copying " CURRENT_OP_FMT, CURRENT_OP_ARGS);
                break;
            }
        }
        ff_log(FC_TRACE, 0, "%scopy " CURRENT_OP_FMT " = ok",
               (simulated ? "(simulated) " : ""), CURRENT_OP_ARGS);
    } while (0);
    return err;
}


/* return (-)EOVERFLOW if request from/to + length overflow specified maximum value */
int fr_io_posix::validate(const char * type_name, ft_uoff type_max, fr_dir_posix dir2, ft_uoff from, ft_uoff to, ft_uoff length)
{
    fr_dir dir;
    switch (dir2) {
        case FC_POSIX_STORAGE2DEV: dir = FC_STORAGE2DEV; break;
        case FC_POSIX_DEV2STORAGE: dir = FC_DEV2STORAGE; break;
        case FC_POSIX_DEV2RAM:
        case FC_POSIX_RAM2DEV:     dir = FC_DEV2DEV;     break;
        default:                   dir = FC_INVALID2INVALID; break;
    }
    return super_type::validate(type_name, type_max, dir, from, to, length);
}


/**
 * flush any I/O specific buffer
 * return 0 if success, else error
 * implementation: call msync() because we use a mmapped() buffer for STORAGE,
 * and call sync() because we write() to DEVICE
 */
int fr_io_posix::flush_bytes()
{
    int err = 0;
    do {
        if (ui() != NULL)
            ui()->show_io_flush();

        if (simulate_run())
            break;

#if 1
        if ((err = msync(storage_mmap, storage_mmap_size, MS_SYNC)) != 0) {
            ff_log(FC_WARN, errno, "I/O error in %s msync(address + %" FT_ULL ", length = %" FT_ULL ")",
                    label[FC_STORAGE], (ft_ull)0, (ft_ull)storage_mmap_size);
            err = 0;
        }
#else
        fr_vector<ft_uoff>::const_iterator begin = primary_storage().begin(), iter, end = primary_storage().end();
        for (iter = begin; iter != end; ++iter)
            msync_bytes(*iter);
        if (secondary_storage().length() != 0)
            msync_bytes(secondary_storage());
#endif

        (void) sync(); // sync() returns void
#if 0
        if (err != 0) {
            ff_log(FC_WARN, errno, "I/O error in sync()");
            err = 0;
        }
#endif
    } while (0);
    return err;
}

/** internal method, called by flush_bytes() to perform msync() on mmapped storage */
int fr_io_posix::msync_bytes(const fr_extent<ft_uoff> & extent) const
{
    ft_uoff mem_offset = extent.second.user_data;
    ft_size mem_length = (ft_size) extent.second.length; // check for overflow?
    int err;
    if ((err = msync((char *)storage_mmap + mem_offset, mem_length, MS_SYNC)) != 0) {
        ff_log(FC_WARN, errno, "I/O error in %s msync(address + %" FT_ULL ", length = %" FT_ULL ")",
                label[FC_STORAGE], (ft_ull)mem_offset, (ft_ull)mem_length);
        err = 0;
    }
    return err;
}

/**
 * write zeroes to device (or to storage).
 * used to remove device-renumbered blocks once remapping is finished
 */
int fr_io_posix::zero_bytes(fr_to to, ft_uoff offset, ft_uoff length)
{
    static char * zero_buf = NULL;
    enum { ZERO_BUF_LEN = 1024*1024 };
    ft_uoff max = to == FC_TO_DEV ? dev_length() : (ft_uoff) storage_mmap_size;
    int err = 0;
    do {
        if (!ff_can_sum(offset, length) || length > max || offset > max - length) {
            err = ff_log(FC_FATAL, EOVERFLOW, "internal error! %s io.zero(to = %d, offset = %" FT_ULL ", length = %" FT_ULL ")"
                         " overflows maximum allowed %" FT_ULL ,
                         label[to == FC_TO_DEV ? FC_DEVICE : FC_STORAGE],
                         (int)to, (ft_ull)offset, (ft_ull)length, (ft_ull)max);
            break;
        }
        if (ui() != NULL)
            ui()->show_io_write(to, offset, length);
        if (simulate_run())
            break;

        if (to == FC_TO_STORAGE) {
            memset((char *) storage_mmap + (ft_size)offset, '\0', (ft_size)length);
            break;
        }
        /* else (to == FC_TO_DEVICE) */

        if (zero_buf == NULL) {
            if ((zero_buf = (char *) malloc(ZERO_BUF_LEN)) == NULL)
                return ENOMEM;
            memset(zero_buf, '\0', ZERO_BUF_LEN);
        }
        int dev_fd = fd[FC_DEVICE];
        if ((err = ff_posix_lseek(dev_fd, offset)) != 0) {
            err = ff_log(FC_ERROR, err, "error in %s lseek(fd = %d, offset = %" FT_ULL ")", label[FC_DEVICE], dev_fd, (ft_ull) offset);
            break;
        }
        ft_uoff chunk;
        while (length != 0) {
            chunk = ff_min2<ft_uoff>(length, ZERO_BUF_LEN);
            if ((err = ff_posix_write(dev_fd, zero_buf, chunk)) != 0) {
                err = ff_log(FC_ERROR, err, "error in %s write({fd = %d, offset = %" FT_ULL "}, zero_buffer, length = %" FT_ULL ")",
                             label[FC_DEVICE], dev_fd, (ft_ull) offset, (ft_ull) chunk);
                break;
            }
            length -= chunk;
        }
    } while (0);
    return err;
}

/**
 * write zeroes to primary storage.
 * used to remove primary-storage once remapping is finished
 * and clean the remaped file-system
 */
int fr_io_posix::zero_primary_storage()
{
    fr_vector<ft_uoff>::const_iterator begin = primary_storage().begin(), iter, end = primary_storage().end();
    ft_size mem_offset, mem_length;

    const bool simulated = simulate_run();
    FT_UI_NS fr_ui * this_ui = ui();

    for (iter = begin; iter != end; ++iter) {
        const fr_extent<ft_uoff> & extent = *iter;
        mem_offset = extent.second.user_data;
        mem_length = (ft_size) extent.second.length; // check for overflow?

        if (this_ui != NULL)
            this_ui->show_io_write(FC_TO_STORAGE, mem_offset, mem_length);

        if (!simulated)
            memset((char *) storage_mmap + mem_offset, '\0', mem_length);
    }
    return 0;
}


FT_IO_NAMESPACE_END
