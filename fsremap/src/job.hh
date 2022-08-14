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
 * log.hh
 *
 *  Created on: Mar 8, 2011
 *      Author: max
 */

#ifndef FSREMAP_JOB_HH
#define FSREMAP_JOB_HH

#include "types.hh"    // for ft_size, ft_uint, ft_string

#if defined(FT_HAVE_STDIO_H)
# include <stdio.h>        // for FILE. also for sprintf() used in job.cc
#elif defined(FT_HAVE_CSTDIO)
# include <cstdio>         // for FILE. also for sprintf() used in job.cc
#endif

#include "args.hh"     // for fr_args, FC_STORAGE_SIZE_N
#include "log.hh"      // for ft_log_appender

FT_NAMESPACE_BEGIN

class fr_job
{
private:
    ft_string this_dir;
    ft_size this_storage_size[FC_STORAGE_SIZE_N];

    FILE * this_log_file;
    ft_log_appender * this_log_appender;
    ft_uint this_id;
    fr_clear_free_space this_clear;
    bool this_force_run, this_simulate_run, this_resume_job, this_ask_questions;

    /** initialize logging subsystem */
    int init_log();

public:
    /** default constructor */
    fr_job();

    /** destructor. calls quit() */
    ~fr_job();

    /** initialize this job, or return error */
    int init(const fr_args & args);

    /** quit this job */
    void quit();

    /** return job_id, or 0 if not set */
    FT_INLINE ft_uint job_id() const { return this_id; }

    /** return job_dir, or empty if not set */
    FT_INLINE const ft_string & job_dir() const { return this_dir; }

    /** return secondary storage, buffer, or primary/secondary exact length to use (in bytes). 0 means autodetect */
    FT_INLINE ft_size job_storage_size(fr_storage_size which) const { return this_storage_size[which]; }

    /** set secondary storage, buffer, or primary/secondary exact length to use (in bytes), or 0 to activate autodetection */
    FT_INLINE void job_storage_size(fr_storage_size which, ft_size len) { this_storage_size[which] = len; }


    /**
     * return which free blocks to clear after remapping:
     * all, only blocks used as primary storage or renumbered device, or none
     */
    FT_INLINE fr_clear_free_space job_clear() const { return this_clear; }

    /**
     * set which free blocks to clear after remapping:
     * all, only blocks used as primary storage or renumbered device, or none
     */
    FT_INLINE void job_clear(fr_clear_free_space clear) { this_clear = clear; }


    /**
     * return true if I/O classes should be less strict on sanity checks
     * and generate WARNINGS (and keep going) for failed sanity checks
     * instead of generating ERRORS (and quitting)
     */
    FT_INLINE bool force_run() const { return this_force_run; }

    /**
     * set to true if I/O classes should be less strict on sanity checks
     * and generate WARNINGS (and keep going) for failed sanity checks
     * instead of generating ERRORS (and quitting)
     */
    FT_INLINE void force_run(bool force_flag) { this_force_run = force_flag; }

    /** return true if I/O classes should simulate run, i.e. run WITHOUT reading or writing device blocks */
    FT_INLINE bool simulate_run() const { return this_simulate_run; }

    /** set to true if I/O classes should simulate run, i.e. run WITHOUT reading or writing device blocks */
    FT_INLINE void simulate_run(bool simulate_flag) { this_simulate_run = simulate_flag; }

    /** return true if resuming an existing job */
    FT_INLINE bool resuming_job() const { return this_resume_job; }

    /** return true if classes can ask questions to the user and read answers from stdin */
    FT_INLINE bool ask_questions() const { return this_ask_questions; }

    /** set to true if classes can ask questions to the user and read answers from stdin */
    FT_INLINE void ask_questions(bool ask_flag) { this_ask_questions = ask_flag; }
};

FT_NAMESPACE_END


#endif /* FSREMAP_JOB_HH */
