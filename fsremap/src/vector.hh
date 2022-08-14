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
 * vector.hh
 *
 *  Created on: Feb 27, 2011
 *      Author: max
 */

#ifndef FSREMAP_VECTOR_HH
#define FSREMAP_VECTOR_HH

#include "check.hh"

#include <vector>      // for std::vector<T> */

#include "fwd.hh"      // for fr_map<T>
#include "log.hh"      // for ft_log_level, FC_SHOW_DEFAULT_LEVEL. also used by vector.t.hh for ff_log()
#include "extent.hh"   // for fr_extent<T>


FT_NAMESPACE_BEGIN

template<typename T>
class fr_vector : public std::vector<fr_extent<T> >
{
private:
    typedef std::vector<fr_extent<T> > super_type;

    /** actual implementation of compose() below */
    int compose0(const fr_vector<T> & a2b, const fr_vector<T> & a2c, T & ret_block_size_bitmask, fr_vector<T> * unmapped = 0);

public:
    typedef fr_extent_key<T>      key_type;
    typedef fr_extent_payload<T>  mapped_type;

    typedef typename super_type::value_type     value_type;
    typedef typename super_type::iterator       iterator;
    typedef typename super_type::const_iterator const_iterator;

    /**
     * append a single extent to this vector.
     *
     * if this vector is not empty
     * and specified extent ->physical is equal to last->physical + last->length
     * and specified extent ->logical  is equal to last->logical  + last->length
     * where 'last' is the last extent in this vector,
     * then merge the two extents
     *
     * otherwise append to this vector a new extent containing specified extent (physical, logical, length)
     */
    void append(T physical, T logical, T length, ft_size user_data);

    /**
     * append a single extent to this vector.
     *
     * if this vector is not empty
     * and specified extent ->physical is equal to last->physical + last->length
     * and specified extent ->logical  is equal to last->logical  + last->length
     * where 'last' is the last extent in this vector,
     * then merge the two extents
     *
     * otherwise append to this vector a new extent containing specified extent (physical, logical, length, user_data)
     */
    FT_INLINE void append(const typename value_type::super_type & extent)
    {
        append(extent.first.physical, extent.second.logical, extent.second.length, extent.second.user_data);
    }

    /**
     * append another extent vector to this vector.
     *
     * this method does not merge extents: the two lists of extents will be simply concatenated
     */
    void append_all(const fr_vector<T> & other);

    /**
     * reorder this vector in-place, sorting by physical
     */
    void sort_by_physical();
    void sort_by_physical(iterator from, iterator to);

    /**
     * reorder this vector in-place, sorting by logical
     */
    void sort_by_logical();
    void sort_by_logical(iterator from, iterator to);

    /**
     * reorder this vector in-place, sorting by reverse length (largest extents will be first)
     */
    void sort_by_reverse_length();
    void sort_by_reverse_length(iterator from, iterator to);

    /**
     * swap ->physical with ->logical in each extent of this vector.
     * Note: does NOT sort after swapping!
     */
    void transpose();

    /**
     * used by ft_io_prealloc.
     *
     * truncate at specified logical value
     */
   void truncate_at_logical(T logical_end);

    /**
     * used by ft_io_prealloc.
     *
     * given a vector mapping a->b (v1) and a vector mapping a->c (v2),
     * compute the vector mapping b->c (v2) and append it to this vector.
     *
     * user_data will be copied from v1.
     * all extents in b not mapped to c will be added to 'unmapped' (if not NULL)
     *
     * a->b and a->c must be sorted by ->physical
     * returns error if a->b domain (range in a) is smaller than a->c domain (range in a)
     * and in particular if a->b has holes where a->c does not.
     */
    FT_INLINE int compose(const fr_vector<T> & a2b, const fr_vector<T> & a2c, T & ret_block_size_bitmask, fr_vector<T> & unmapped) {
    	return compose0(a2b, a2c, ret_block_size_bitmask, & unmapped);
    }


    /** same as compose() above, but does not compute 'block_size_bitmask' and 'unmapped' */
    FT_INLINE int compose(const fr_vector<T> & a2b, const fr_vector<T> & a2c) {
    	T block_size_bitmask = 0;
    	return compose0(a2b, a2c, block_size_bitmask);
    }

    /** print vector contents to log */
    void show(const char * label1, const char * label2, ft_uoff effective_block_size, ft_log_level level = FC_SHOW_DEFAULT_LEVEL) const;
};

FT_NAMESPACE_END


#ifdef FT_HAVE_EXTERN_TEMPLATE
#  define FT_TEMPLATE_vector_hh(ft_prefix, T) ft_prefix class FT_NS fr_vector< T >;
   FT_TEMPLATE_DECLARE(FT_TEMPLATE_vector_hh)
#else
#  include "vector.t.hh"
#endif /* FT_HAVE_EXTERN_TEMPLATE */



#endif /* FSREMAP_VECTOR_HH */
