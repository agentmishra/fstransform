/*
 * fstransform - transform a file-system to another file-system type,
 *               preserving its contents and without the need for a backup
 *
 * Copyright (C) 2011-2012 Massimiliano Ghilardi
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
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
 * io/disk_stat.hh
 *
 *  Created on: Oct 05, 2011
 *      Author: max
 */

#ifndef FSMOVE_IO_DISK_STAT_HH
#define FSMOVE_IO_DISK_STAT_HH

#include "../check.hh"
#include "../types.hh" // for ft_uoff, ft_string

FT_IO_NAMESPACE_BEGIN

/**
 * class to keep track of disk total and free space
 */
class fm_disk_stat {
  private:
    enum { _96kbytes = (ft_uoff)96 << 10, _1Gbyte = (ft_uoff)1 << 30 };

    ft_string this_name;
    ft_uoff this_total, this_free;

  public:
    /**
     * if file systems is smaller than 6GB, critically low free space is 96kbytes.
     * if file systems is between 6GB and 64TB, critically low free space is total
     * disk space divided 65536 (i.e. 0.0015%). if file systems is larger than
     * 64TB, critically low free space is 1Gbyte.
     */
    enum {
        // THRESHOLD_MIN must be somewhat larger than fm_io_posix::FT_BUFSIZE
        // (currently 64k)
        THRESHOLD_MIN = _96kbytes,
        THRESHOLD_MAX = _1Gbyte,
    };
    /** constructor */
    fm_disk_stat();

    /** compiler-generated copy constructor is fine */
    // const fm_disk_stat & fm_disk_stat(const fm_disk_stat &);

    /** compiler-generated destructor is fine */
    // ~fm_disk_stat();

    /** compiler-generated assignment operator is fine */
    // const fm_disk_stat & operator=(const fm_disk_stat &);

    /** clear all data stored in this object */
    void clear();

    /** get the disk name */
    FT_INLINE const ft_string &get_name() const {
        return this_name;
    }
    /** set the disk name */
    FT_INLINE void set_name(const ft_string &name) {
        this_name = name;
    }

    /** return the total disk space */
    FT_INLINE ft_uoff get_total() const {
        return this_total;
    }
    /** set the total disk space */
    FT_INLINE void set_total(ft_uoff total) {
        this_total = total;
    }

    /** return the free disk space */
    FT_INLINE ft_uoff get_free() const {
        return this_free;
    }

    /**
     * set the free disk space.
     * returns 0, or error if free disk space becomes critically low
     */
    int set_free(ft_uoff free);

    /**
     * return true if 'free' amount of free space would trigger a 'critically low
     * free space' error
     */
    bool is_too_low_free_space(ft_uoff free) const;

    /** return the used disk space */
    FT_INLINE ft_uoff get_used() const {
        return this_free < this_total ? this_total - this_free : 0;
    }
};

FT_IO_NAMESPACE_END

#endif /* FSMOVE_IO_IO_HH */
