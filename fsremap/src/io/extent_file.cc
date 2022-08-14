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
 * io/extent_file.cc
 *
 *  Created on: Mar 3, 2011
 *      Author: max
 */

#include "../first.hh"

#if defined(FT_HAVE_ERRNO_H)
# include <errno.h>      // for errno, ENOMEM, EINVAL, EFBIG
#elif defined(FT_HAVE_CERRNO)
# include <cerrno>       // for errno, ENOMEM, EINVAL, EFBIG
#endif

#include "../types.hh"       // for ft_off
#include "../extent.hh"      // for fr_extent<T>
#include "../vector.hh"      // for fr_vector<T>
#include "extent_file.hh"    // for ff_read_extents_file()


FT_IO_NAMESPACE_BEGIN

/**
 * load file blocks allocation map (extents) previously saved into specified file
 * and appends them to ret_container (retrieves also user_data)
 * in case of failure returns errno-compatible error code, and ret_list contents will be UNDEFINED.
 *
 * implementation: simply reads the list of triplets (physical, logical, length)
 * stored in the stream as decimal numbers
 */
int ff_load_extents_file(FILE * f, fr_vector<ft_uoff> & ret_list, ft_uoff & ret_block_size_bitmask)
{
    {
        char header[200];
        for (ft_size i = 0; i < 6; i++) {
            if (fgets(header, sizeof(header), f) == NULL || header[0] != '#')
                return EPROTO;
        }
    }

    ft_ull physical, logical, length = 0, user_data;
    if (fscanf(f, "count %" FT_ULL "\n", & length) != 1)
        return EPROTO;

    if (fscanf(f, "physical\tlogical\tlength\tuser_data\n") < 0)
        return EPROTO;

    ft_uoff block_size_bitmask = ret_block_size_bitmask;
    ft_size i = ret_list.size(), n = (ft_size) length;

    ret_list.resize(n += i);
    for (; i < n; i++) {
        if (fscanf(f, "%" FT_ULL " %" FT_ULL " %" FT_ULL " %" FT_ULL "\n", &physical, &logical, &length, &user_data) != 4)
            return EPROTO;

        fr_extent<ft_uoff> & extent = ret_list[i];

        block_size_bitmask |=
            (extent.physical() = (ft_uoff) physical) |
            (extent.logical()  = (ft_uoff) logical) |
            (extent.length()   = (ft_uoff) length);

        extent.user_data() = (ft_size) user_data;
    }
    ret_block_size_bitmask = block_size_bitmask;
    return 0;
}


/**
 * writes file blocks allocation map (extents) to specified FILE (stores also user_data)
 * in case of failure returns errno-compatible error code.
 *
 * implementation: simply writes the list of triplets (physical, logical, length)
 * into the FILE as decimal numbers
 */
int ff_save_extents_file(FILE * f, const fr_vector<ft_uoff> & extent_list)
{
    fprintf(f, "%s",
            "################################################################################\n"
            "######################  DO NOT EDIT THIS FILE ! ################################\n"
            "################################################################################\n"
            "############# This file was automatically generated by fsremap.     ############\n"
            "############# Any change you may do will CORRUPT resuming this job! ############\n"
            "################################################################################\n");
    fprintf(f, "count %" FT_ULL "\n", (ft_ull) extent_list.size());
    fprintf(f, "physical\tlogical\tlength\tuser_data\n");

    fr_vector<ft_uoff>::const_iterator iter = extent_list.begin(), end = extent_list.end();
    for (; iter != end; ++iter) {
        const fr_extent<ft_uoff> & extent = *iter;
        if (fprintf(f, "%" FT_ULL "\t%" FT_ULL "\t%" FT_ULL "\t%" FT_ULL "\n",
                    (ft_ull)extent.physical(), (ft_ull)extent.logical(), (ft_ull)extent.length(), (ft_ull)extent.user_data())
                    <= 0)
            break;
    }
    return iter == end ? 0 : errno;
}


FT_IO_NAMESPACE_END
