/* upstart
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef INIT_JOB_H
#define INIT_JOB_H

#include <sys/types.h>
#include <sys/resource.h>

#include <time.h>
#include <stdio.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/timer.h>
#include <nih/main.h>

#include <upstart/enum.h>

#include "event.h"


/**
 * JOB_DEFAULT_PID_TIMEOUT:
 *
 * The default length of time to wait after spawning a daemon process for
 * the pid to be obtained before giving up and assuming the job did not
 * start.
 **/
#define JOB_DEFAULT_PID_TIMEOUT 10

/**
 * JOB_DEFAULT_KILL_TIMEOUT:
 *
 * The default length of time to wait after sending a process the TERM
 * signal before sending the KILL signal if it hasn't terminated.
 **/
#define JOB_DEFAULT_KILL_TIMEOUT 5

/**
 * JOB_DEFAULT_RESPAWN_LIMIT:
 *
 * The default number of times in JOB_DEFAULT_RESPAWN_INTERVAL seconds that
 * we permit a process to respawn before stoping it
 **/
#define JOB_DEFAULT_RESPAWN_LIMIT 3

/**
 * JOB_DEFAULT_RESPAWN_INTERVAL:
 *
 * The default number of seconds before resetting the respawn timer.
 **/
#define JOB_DEFAULT_RESPAWN_INTERVAL 300

/**
 * JOB_DEFAULT_UMASK:
 *
 * The default file creation mark for processes.
 **/
#define JOB_DEFAULT_UMASK 022


/**
 * JobProcess:
 * @script: whether a shell will be required,
 * @command: command or script to be run,
 * @pid: current process id.
 *
 * This structure represents an individual action process within the job.
 * When @script is FALSE, @command is checked for shell characters; if there
 * are none, it is split on whitespace and executed directly using exec().
 * If there are shell characters, or @script is TRUE, @command is executed
 * using a shell.
 *
 * If the process is running, the current pid will be stored in @pid.
 **/
typedef struct job_process {
	int    script;
	char  *command;
	pid_t  pid;
} JobProcess;

/**
 * Job:
 * @entry: list header,
 * @id: unique job id,
 * @name: string name of the job; namespace shared with events,
 * @description: description of the job; intended for humans,
 * @author: author of the job; intended for humans,
 * @version: version of the job; intended for humans,
 * @replacement: job to replace this one,
 * @replacement_for: job this is a replacement for,
 * @instance_of: job this is an instance of,
 * @goal: whether the job is to be stopped or started,
 * @state: actual state of the job,
 * @cause: cause of last goal change,
 * @blocked: emitted event we're waiting to finish,
 * @failed: whether the last process ran failed,
 * @failed_process: the last process that failed,
 * @exit_status: exit status of the last failed process,
 * @start_events: list of events that can start this job,
 * @stop_events; list of events that can stop this job.
 * @emits: list of additional events that this job can emit,
 * @process: processes to be run,
 * @kill_timeout: time to wait between sending TERM and KILL signals,
 * @kill_timer: timer to kill process,
 * @instance: job is always waiting and spawns instances,
 * @service: job has reached its goal when running,
 * @respawn: process should be restarted if it fails,
 * @respawn_limit: number of respawns in @respawn_interval that we permit,
 * @respawn_interval: barrier for @respawn_limit,
 * @respawn_count: number of respawns since @respawn_time,
 * @respawn_time: time service was first respawned,
 * @normalexit: array of exit codes that prevent a respawn,
 * @normalexit_len: length of @normalexit array,
 * @daemon: process forks into background; pid needs to be obtained,
 * @pid_file: obtain pid by reading this file,
 * @pid_binary: obtain pid by locating this binary,
 * @pid_timeout: time to wait before giving up obtaining pid,
 * @pid_timer: timer for pid location,
 * @console: how to arrange the job's stdin/out/err file descriptors,
 * @env: NULL-terminated list of environment strings to set,
 * @umask: file mode creation mask,
 * @nice: process priority,
 * @limits: resource limits indexed by resource,
 * @chroot: root directory of process (implies @chdir if not set),
 * @chdir: working directory of process,
 *
 * This structure represents a known task or service that should be tracked
 * by the init daemon; as tasks and services are fundamentally identical,
 * except for the handling when the main process terminates, they are both
 * collated together in this structure and only differ in the value of the
 * @service member.
 **/
typedef struct job Job;
struct job {
	NihList         entry;
	uint32_t        id;

	char           *name;
	char           *description;
	char           *author;
	char           *version;

	Job            *replacement;
	Job            *replacement_for;
	Job            *instance_of;

	JobGoal         goal;
	JobState        state;

	EventEmission  *cause;
	EventEmission  *blocked;

	int             failed;
	ProcessType     failed_process;
	int             exit_status;

	NihList         start_events;
	NihList         stop_events;
	NihList         emits;

	JobProcess    **process;

	int            *normalexit;
	size_t          normalexit_len;

	time_t          kill_timeout;
	NihTimer       *kill_timer;

	int             instance;
	int             service;
	int             respawn;
	int             respawn_limit;
	time_t          respawn_interval;
	int             respawn_count;
	time_t          respawn_time;

	int             daemon;
	char           *pid_file;
	char           *pid_binary;
	time_t          pid_timeout;
	NihTimer       *pid_timer;

	ConsoleType     console;
	char          **env;

	mode_t          umask;
	int             nice;
	struct rlimit  *limits[RLIMIT_NLIMITS];
	char           *chroot;
	char           *chdir;
};


NIH_BEGIN_EXTERN

uint32_t job_id;
int      job_id_wrapped;
NihHash *jobs;


void        job_init                  (void);

JobProcess *job_process_new           (const void *parent)
	__attribute__ ((warn_unused_result, malloc));
JobProcess *job_process_copy          (const void *parent,
				       const JobProcess *old_process)
	__attribute__ ((warn_unused_result, malloc));

Job *       job_new                   (const void *parent, const char *name)
	__attribute__ ((warn_unused_result, malloc));
Job *       job_copy                  (const void *parent, const Job *old_job)
	__attribute__ ((warn_unused_result, malloc));

Job *       job_find_by_name          (const char *name);
Job *       job_find_by_pid           (pid_t pid, ProcessType *process);
Job *       job_find_by_id            (uint32_t id);

Job *       job_instance              (Job *job);
void        job_change_goal           (Job *job, JobGoal goal,
				       EventEmission *emission);

void        job_change_state          (Job *job, JobState state);
JobState    job_next_state            (Job *job);

int         job_should_replace        (Job *job);

void        job_run_process           (Job *job, ProcessType process);
void        job_kill_process          (Job *job, ProcessType process);

void        job_child_reaper          (void *ptr, pid_t pid, int
				       killed, int status);

void        job_handle_event          (EventEmission *emission);
void        job_handle_event_finished (EventEmission *emission);

void        job_detect_stalled        (void);
void        job_free_deleted          (void);

NIH_END_EXTERN

#endif /* INIT_JOB_H */
