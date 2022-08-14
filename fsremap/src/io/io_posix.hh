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
 * io/io_posix.hh
 *
 *  Created on: Feb 28, 2011
 *      Author: max
 */

#ifndef FSREMAP_IO_IO_POSIX_HH
#define FSREMAP_IO_IO_POSIX_HH

#include "../types.hh"    /* for ft_uoff */
#include "io.hh"          /* for fr_io   */


FT_IO_NAMESPACE_BEGIN

/**
 * class performing I/O on POSIX systems
 */
class fr_io_posix: public fr_io
{
public:
    enum {
        FC_DEVICE = fr_io::FC_DEVICE,
        FC_LOOP_FILE = fr_io::FC_LOOP_FILE,
        FC_ZERO_FILE = fr_io::FC_ZERO_FILE,
        FC_FILE_COUNT = 3, // must be equal to count of preceding enum constants,
        FC_SECONDARY_STORAGE = fr_io::FC_SECONDARY_STORAGE,
        FC_ALL_FILE_COUNT = 4,
        FC_PRIMARY_STORAGE = fr_io::FC_PRIMARY_STORAGE,
        FC_STORAGE = fr_io::FC_STORAGE,
        FC_FREE_SPACE = fr_io::FC_FREE_SPACE,
    };

private:
    typedef fr_io super_type;

    /** direction of copy_bytes() operations */
    enum fr_dir_posix {
        FC_POSIX_STORAGE2DEV,
        FC_POSIX_DEV2STORAGE,
        FC_POSIX_DEV2RAM,
        FC_POSIX_RAM2DEV,
    };


    int fd[FC_ALL_FILE_COUNT];
    void * storage_mmap, * buffer_mmap;
    ft_size storage_mmap_size, buffer_mmap_size;

    /* device major/minor numbers */
    ft_dev this_dev_blkdev;

    /** open DEVICE */
    int open_dev(const char * path);

    /** really open DEVICE */
    int open_dev0(const char * path, int * ret_fd, ft_dev * ret_dev, ft_uoff * ret_len);

    /** open LOOP-FILE or ZERO-FILE */
    int open_file(ft_size i, const char * path);

    /** set device major/minor numbers */
    FT_INLINE void dev_blkdev(ft_dev blkdev) { this_dev_blkdev = blkdev; }

protected:

    /** return true if a single descriptor/stream is open */
    bool is_open0(ft_size which) const;

    /** close a single descriptor/stream */
    void close0(ft_size which);

    /** return device major/minor numbers, or 0 if not known */
    FT_INLINE ft_dev dev_blkdev() const { return this_dev_blkdev; }

    /** return true if this I/O has open descriptors/streams to LOOP-FILE and FREE-SPACE */
    bool is_open_extents() const;

    /* return (-)EOVERFLOW if request from/to + length overflow specified maximum value */
    static int validate(const char * type_name, ft_uoff type_max, fr_dir_posix dir, ft_uoff from, ft_uoff to, ft_uoff length);

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
    int read_extents_loop_file(fr_vector<ft_uoff> & loop_file_extents,
                               fr_vector<ft_uoff> & to_zero_extents,
                               ft_uoff & ret_block_size_bitmask);

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
    int read_extents_free_space(const fr_vector<ft_uoff> & loop_file_extents,
                                fr_vector<ft_uoff> & free_space_extents,
                                fr_vector<ft_uoff> & to_zero_extents,
                                ft_uoff & ret_block_size_bitmask);

    /**
     * retrieve LOOP-FILE extents, FREE-SPACE extents and any additional extents to be ZEROED
     * and insert them into the vectors loop_file_extents, free_space_extents and to_zero_extents
     * the vectors will be ordered by extent ->logical (for to_zero_extents, ->physical and ->logical will be the same).
     *
     * return 0 for success, else error (and vectors contents will be UNDEFINED).
     *
     * if success, also update the parameter ret_effective_block_size_log2 to be the log2()
     * of device effective block size.
     * the device effective block size is defined as follows:
     * it is the largest power of 2 that exactly divides all physical,
     * logical and lengths in all returned extents (both for LOOP-FILE
     * and for FREE-SPACE) and that also exactly exactly divides device length.
     *
     * the trick fr_io_posix uses to implement this method
     * is to fill the device's free space with a ZERO-FILE,
     * and actually retrieve the extents used by ZERO-FILE.
     */
    virtual int read_extents(fr_vector<ft_uoff> & loop_file_extents,
                             fr_vector<ft_uoff> & free_space_extents,
                             fr_vector<ft_uoff> & to_zero_extents,
                             ft_uoff & ret_block_size_bitmask);


    /**
     * replace a part of the mmapped() storage_mmap area with specified storage_extent,
     * and store mmapped() address into storage_extent.user_data().
     * return 0 if success, else error.
     *
     * note: fd shoud be this->fd[FC_DEVICE] for primary storage,
     * or this->fd[FC_SECONDARY_STORAGE] for secondary storage
     */
    int replace_storage_mmap(int fd, const char * label, fr_extent<ft_uoff> & storage_extent,
                             ft_size extent_index, ft_size & mem_offset);

    /**
     * create and open SECONDARY-STORAGE in job.job_dir() + '.storage.bin'
     * and fill it with 'secondary_len' bytes of zeros. do not mmap() it.
     * return 0 if success, else error
     */
    int create_secondary_storage(ft_size secondary_len);

    /**
     * actually copy a list of fragments from DEVICE to STORAGE, or from STORAGE or DEVICE, or from DEVICE to DEVICE.
     * note: parameters are in bytes!
     * return 0 if success, else error.
     */
    virtual int flush_copy_bytes(fr_dir dir, fr_vector<ft_uoff> & request_vec);

    /** internal method called by flush_copy_bytes() to read/write from DEVICE to mmapped() memory (either RAM or STORAGE) */
    int flush_copy_bytes(fr_dir_posix dir2, const fr_extent<ft_uoff> & request);

    /** internal method called by flush_copy_bytes() to read/write from DEVICE to mmapped() memory (either RAM or STORAGE) */
    int flush_copy_bytes(fr_dir_posix dir, ft_uoff from, ft_uoff to, ft_uoff length);



    /**
     * flush any I/O specific buffer
     * return 0 if success, else error
     * implementation: call msync() because we use a mmapped() buffer for copy()
     * and call sync() because we write() to DEVICE
     */
    virtual int flush_bytes();

    /** internal method, called by flush_bytes() to perform msync() on mmapped storage */
    int msync_bytes(const fr_extent<ft_uoff> & extent) const;

    /**
     * write zeroes to device (or to storage).
     * used to remove device-renumbered blocks once remapping is finished
     */
    virtual int zero_bytes(fr_to to, ft_uoff offset, ft_uoff length);

public:
    /** constructor */
    fr_io_posix(fr_persist & persist);

    /** destructor. calls close() */
    virtual ~fr_io_posix();

    /** check for consistency and open DEVICE, LOOP-FILE and ZERO-FILE */
    virtual int open(const fr_args & args);

    /** return true if this fr_io_posix is currently (and correctly) open */
    virtual bool is_open() const;

    /** close this I/O, including file descriptors to DEVICE, LOOP-FILE, ZERO-FILE and SECONDARY-STORAGE */
    virtual void close();

    /**
     * close the file descriptors for LOOP-FILE and ZERO-FILE
     */
    virtual void close_extents();

    /**
     * create and open SECONDARY-STORAGE in job.job_dir() + '.storage',
     * fill it with 'secondary_len' bytes of zeros and mmap() it.
     *
     * then mmap() together into consecutive RAM this->primary_storage extents and secondary_storage extents.
     *
     * return 0 if success, else error
     */
    virtual int create_storage(ft_size secondary_len, ft_size mem_buffer_len);

    /** call umount(8) on dev_path() */
    virtual int umount_dev();

    /**
     * called once by work<T>::relocate() immediately before starting the remapping phase.
     *
     * checks that last device block to be written is actually writable.
     * Reason: at least on Linux, if a filesystems is smaller than its containing device, it often limits to its length the writable blocks in the device.
     */
    int check_last_block();

    /**
     * write zeroes to primary storage.
     * used to remove primary-storage once remapping is finished
     * and clean the remaped file-system
     */
    virtual int zero_primary_storage();

    /** close and munmap() SECONDARY-STORAGE. called by close() and by work<T>::close_storage() */
    virtual int close_storage();

    /** called to remove SECONDARY-STORAGE from file system if execution is completed successfully */
    virtual int remove_storage_after_success();
};

FT_IO_NAMESPACE_END

#endif /* FSREMAP_IO_IO_POSIX_HH */
