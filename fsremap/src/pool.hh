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
 * pool.hh
 *
 *  Created on: Mar 8, 2011
 *      Author: max
 */

#ifndef FSREMAP_POOL_HH
#define FSREMAP_POOL_HH

#include "check.hh"

#include <map>            // for std::map<K,V>
#include <vector>         // for std::vector<T>

#include "map.hh"         // for fr_map<T>

FT_NAMESPACE_BEGIN


template<typename T>
class fr_pool_entry : public std::vector<typename fr_map<T>::iterator>
{ };


/**
 * pool of extents, ordered by ->length. the pool is backed by a fr_map<T>,
 * so that modifications to the pool are propagated to the backing fr_map<T>
 *
 * used for best-fit allocation of free space, when free space is represented
 * by a fr_map<T> of extents.
 */
template<typename T>
class fr_pool : private std::map<T, fr_pool_entry<T> >
{
private:
    typedef std::map<T, fr_pool_entry<T> > super_type;

    typedef typename fr_map<T>::iterator    map_iterator;
    typedef typename fr_map<T>::key_type    map_key_type;
    typedef typename fr_map<T>::mapped_type map_mapped_type;
    typedef typename fr_map<T>::value_type  map_value_type;

public:
    typedef typename super_type::key_type       key_type;
    typedef typename super_type::mapped_type    mapped_type;
    typedef typename super_type::value_type     value_type;
    typedef typename super_type::iterator       iterator;
    typedef typename super_type::const_iterator const_iterator;

private:
    fr_map<T> & backing_map;

    /** initialize this pool to reflect contents of backing fr_map<T> */
    void init();

    /** insert into this pool an extent _ALREADY_ present in backing map */
    void insert0(map_iterator map_iter);

    /**
     * "allocate" from the single extent 'iter' in this pool and shrink it
     * to store the single extent 'map_iter'.
     * remove allocated (and renumbered) extent from map and write it into map_allocated
     */
    void allocate_unfragmented(map_iterator map_iter, fr_map<T> & map, fr_map<T> & map_allocated, iterator iter);

    /**
     * "allocate" a single fragment from this pool to store the single extent 'map_iter'.
     * shrink extent from map (leaving unallocated portion) and write the allocated portion into map_allocated.
     *
     * return iterator to remainder of extent that still needs to be allocated
     */
    map_iterator allocate_fragment(map_iterator map_iter, fr_map<T> & map, fr_map<T> & map_allocated);

public:
    fr_pool(fr_map<T> & map);


    /*
     * "allocate" (and remove) extents from this pool to store 'map' extents using a best-fit strategy.
     * remove allocated (and renumbered) extents from 'map' and write them into 'map_allocated',
     * fragmenting them if needed
     */
    void allocate_all(fr_map<T> & map, fr_map<T> & map_allocated);

    /**
     * "allocate" using a best-fit strategy (and remove) extents from this pool
     * to store the single extent 'map_iter', which must belong to 'map'.
     * remove allocated (and renumbered) extent from map and write it into map_allocated,
     * fragmenting it if needed.
     */
    void allocate(map_iterator map_iter, fr_map<T> & map, fr_map<T> & map_allocated);
};

FT_NAMESPACE_END


#ifdef FT_HAVE_EXTERN_TEMPLATE
#  define FT_TEMPLATE_pool_hh(ft_prefix, T)     ft_prefix class FT_NS fr_pool< T >;
   FT_TEMPLATE_DECLARE(FT_TEMPLATE_pool_hh)
#else
#  include "pool.t.hh"
#endif /* FT_HAVE_EXTERN_TEMPLATE */


#endif /* FSREMAP_POOL_HH */
