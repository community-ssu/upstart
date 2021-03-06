/* upstart
 *
 * cfgfile.c - configuration and job file parsing
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/watch.h>
#include <nih/config.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/enum.h>

#include "job.h"
#include "event.h"
#include "cfgfile.h"
#include "errors.h"


/* Prototypes for static functions */
static int   cfg_parse_exec            (JobProcess *proc,
					NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_parse_script          (JobProcess *proc,
					NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_parse_process         (Job *job, ProcessType process,
					NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);

static int   cfg_stanza_exec           (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_script         (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_pre_start      (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_post_start     (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_pre_stop       (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_post_stop      (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);

static int   cfg_stanza_start          (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_stop           (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);

static int   cfg_stanza_description    (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_author         (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_version        (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_emits          (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_daemon         (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_respawn        (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_service	       (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_instance       (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_pid            (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_kill           (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_normal         (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_console        (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_env            (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_umask          (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_nice           (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_limit          (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_chroot         (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);
static int   cfg_stanza_chdir          (Job *job, NihConfigStanza *stanza,
					const char *file, size_t len,
					size_t *pos, size_t *lineno);

static char *cfg_job_name              (const void *parent, const char *prefix,
					const char *dirname, const char *path)
	__attribute__ ((warn_unused_result, malloc));
static void  cfg_create_modify_handler (const char *prefix, NihWatch *watch,
					const char *path,
					struct stat *statbuf);
static void  cfg_delete_handler        (const char *prefix, NihWatch *watch,
					const char *path);
static int   cfg_visitor               (const char *prefix,
					const char *dirname, const char *path,
					struct stat *statbuf)
	__attribute__ ((warn_unused_result));


/**
 * stanzas:
 *
 * This is the table of known configuration file stanzas and the functions
 * that handle parsing them.
 **/
static NihConfigStanza stanzas[] = {
	{ "exec",        (NihConfigHandler)cfg_stanza_exec        },
	{ "script",      (NihConfigHandler)cfg_stanza_script      },
	{ "pre-start",   (NihConfigHandler)cfg_stanza_pre_start   },
	{ "post-start",  (NihConfigHandler)cfg_stanza_post_start  },
	{ "pre-stop",    (NihConfigHandler)cfg_stanza_pre_stop    },
	{ "post-stop",   (NihConfigHandler)cfg_stanza_post_stop   },
	{ "start",       (NihConfigHandler)cfg_stanza_start       },
	{ "stop",        (NihConfigHandler)cfg_stanza_stop        },
	{ "description", (NihConfigHandler)cfg_stanza_description },
	{ "author",      (NihConfigHandler)cfg_stanza_author      },
	{ "version",     (NihConfigHandler)cfg_stanza_version     },
	{ "emits",       (NihConfigHandler)cfg_stanza_emits       },
	{ "daemon",      (NihConfigHandler)cfg_stanza_daemon      },
	{ "respawn",     (NihConfigHandler)cfg_stanza_respawn     },
	{ "service",     (NihConfigHandler)cfg_stanza_service     },
	{ "instance",    (NihConfigHandler)cfg_stanza_instance    },
	{ "pid",         (NihConfigHandler)cfg_stanza_pid         },
	{ "kill",        (NihConfigHandler)cfg_stanza_kill        },
	{ "normal",      (NihConfigHandler)cfg_stanza_normal      },
	{ "console",     (NihConfigHandler)cfg_stanza_console     },
	{ "env",         (NihConfigHandler)cfg_stanza_env         },
	{ "umask",       (NihConfigHandler)cfg_stanza_umask       },
	{ "nice",        (NihConfigHandler)cfg_stanza_nice        },
	{ "limit",       (NihConfigHandler)cfg_stanza_limit       },
	{ "chroot",      (NihConfigHandler)cfg_stanza_chroot      },
	{ "chdir",       (NihConfigHandler)cfg_stanza_chdir       },

	NIH_CONFIG_LAST
};


/**
 * cfg_parse_exec:
 * @proc: job process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse the arguments to a job process's exec
 * stanza from @file, the command and its arguments are expected to follow
 * and will be the command run for the job.
 *
 * The JobProcess for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_parse_exec (JobProcess      *proc,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	nih_assert (proc != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	proc->script = FALSE;
	proc->command = nih_config_parse_command (proc, file, len,
						  pos, lineno);

	if (! proc->command)
		return -1;

	return 0;
}

/**
 * cfg_parse_script:
 * @proc: job process being parsed.
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to parse a script block for a job process's script
 * stanza from @file.  A block terminated with "end script" is expected to
 * follow, and will be stored in the command for the job.
 *
 * The JobProcess for this to be parsed into should have already been
 * allocated.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_parse_script (JobProcess      *proc,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	nih_assert (proc != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (nih_config_skip_comment (file, len, pos, lineno) < 0)
		return -1;

	proc->script = TRUE;
	proc->command = nih_config_parse_block (proc, file, len,
						pos, lineno, "script");

	if (! proc->command)
		return -1;

	return 0;
}

/**
 * cfg_parse_process:
 * @job: job being parsed,
 * @process: which process is being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * This function is used to allocate a JobProcess for @process within @job,
 * and expects either "exec" or "script" to follow, calling cfg_parse_exec()
 * or cfg_parse_script() appropriately.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_parse_process (Job             *job,
		   ProcessType      process,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->process[process])
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	NIH_MUST (job->process[process] = job_process_new (job->process));

	if (! strcmp (arg, "exec")) {
		nih_free (arg);

		return cfg_parse_exec (job->process[process], stanza,
				       file, len, pos, lineno);
	} else if (! strcmp (arg, "script")) {
		nih_free (arg);

		return cfg_parse_script (job->process[process], stanza,
					 file, len, pos, lineno);
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}


/**
 * cfg_stanza_exec:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an exec stanza from @file by allocating the main job process and
 * calling cfg_parse_exec() to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_exec (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->process[PROCESS_MAIN])
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	NIH_MUST (job->process[PROCESS_MAIN] = job_process_new (job->process));

	return cfg_parse_exec (job->process[PROCESS_MAIN], stanza,
			       file, len, pos, lineno);
}

/**
 * cfg_stanza_script:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a script stanza from @file by allocating the main job process and
 * calling cfg_parse_script() to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_script (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->process[PROCESS_MAIN])
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	NIH_MUST (job->process[PROCESS_MAIN] = job_process_new (job->process));

	return cfg_parse_script (job->process[PROCESS_MAIN], stanza,
				 file, len, pos, lineno);
}

/**
 * cfg_stanza_pre_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pre-start stanza from @file by calling cfg_parse_process()
 * with PROCESS_PRE_START to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_pre_start (Job             *job,
		      NihConfigStanza *stanza,
		      const char      *file,
		      size_t           len,
		      size_t          *pos,
		      size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return cfg_parse_process (job, PROCESS_PRE_START, stanza,
				  file, len, pos, lineno);
}

/**
 * cfg_stanza_post_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a post-start stanza from @file by calling cfg_parse_process()
 * with PROCESS_POST_START to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_post_start (Job             *job,
		       NihConfigStanza *stanza,
		       const char      *file,
		       size_t           len,
		       size_t          *pos,
		       size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return cfg_parse_process (job, PROCESS_POST_START, stanza,
				  file, len, pos, lineno);
}

/**
 * cfg_stanza_pre_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pre-stop stanza from @file by calling cfg_parse_process()
 * with PROCESS_PRE_STOP to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_pre_stop (Job             *job,
		     NihConfigStanza *stanza,
		     const char      *file,
		     size_t           len,
		     size_t          *pos,
		     size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return cfg_parse_process (job, PROCESS_PRE_STOP, stanza,
				  file, len, pos, lineno);
}

/**
 * cfg_stanza_post_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a post-stop stanza from @file by calling cfg_parse_process()
 * with PROCESS_POST_STOP to parse it.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_post_stop (Job             *job,
		      NihConfigStanza *stanza,
		      const char      *file,
		      size_t           len,
		      size_t          *pos,
		      size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	return cfg_parse_process (job, PROCESS_POST_STOP, stanza,
				  file, len, pos, lineno);
}


/**
 * cfg_stanza_start:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a start stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an Event structure
 * and stored in the start events list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_start (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		Event *event;
		char  *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		NIH_MUST (event = event_new (job, name));
		nih_free (name);

		event->args = nih_config_parse_args (event, file, len,
						     pos, lineno);
		if (! event->args) {
			nih_free (event);
			return -1;
		}

		nih_list_add (&job->start_events, &event->entry);

		return 0;

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * cfg_stanza_stop:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a stop stanza from @file.  This stanza expects a second "on"
 * argument, followed by an event which is allocated as an Event structure
 * and stored in the stop events list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_stop (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "on")) {
		Event *event;
		char  *name;

		nih_free (arg);

		name = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! name)
			return -1;

		NIH_MUST (event = event_new (job, name));
		nih_free (name);

		event->args = nih_config_parse_args (event, file, len,
						     pos, lineno);
		if (! event->args) {
			nih_free (event);
			return -1;
		}

		nih_list_add (&job->stop_events, &event->entry);

		return 0;

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}


/**
 * cfg_stanza_description:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a description stanza from @file.  This stanza expects a single
 * argument giving a human-readable description of the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_description (Job             *job,
			NihConfigStanza *stanza,
			const char      *file,
			size_t           len,
			size_t          *pos,
			size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->description)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->description = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->description)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_author:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an author stanza from @file.  This stanza expects a single
 * argument giving a human-readable author name for the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_author (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->author)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->author = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->author)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_version:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a version stanza from @file.  This stanza expects a single
 * argument giving a human-readable version number for the job which is
 * stored for later use.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_version (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->version)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->version = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->version)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_emits:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an emits stanza from @file.  This stanza expects one or more
 * arguments giving the names of additional events that can be emitted
 * by this job.
 *
 * Arguments are allocated as Event structures and stored in the emits
 * list of the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_emits (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char **args, **arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (! nih_config_has_token (file, len, pos, lineno))
		nih_return_error (-1, NIH_CONFIG_EXPECTED_TOKEN,
				  _(NIH_CONFIG_EXPECTED_TOKEN_STR));

	args = nih_config_parse_args (NULL, file, len, pos, lineno);
	if (! args)
		return -1;

	for (arg = args; *arg; arg++) {
		Event *event;

		NIH_MUST (event = event_new (job, *arg));
		nih_list_add (&job->emits, &event->entry);
	}

	nih_free (args);

	return 0;
}

/**
 * cfg_stanza_daemon:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file.  This sets the daemon flag for the
 * job and has no arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_daemon (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->daemon)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->daemon = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_respawn:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a daemon stanza from @file.  This either has no arguments, in
 * which case it sets the respawn and service flags for the job, or it has
 * the "limit" argument and sets the respawn rate limit.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_respawn (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	/* Deal with the no-argument form first */
	if (! nih_config_has_token (file, len, pos, lineno)) {
		if (job->respawn)
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		job->respawn = TRUE;
		job->service = TRUE;

		return nih_config_skip_comment (file, len, pos, lineno);
	}


	/* Take the next argument, a sub-stanza keyword. */
	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "limit")) {
		char *endptr;

		nih_free (arg);

		if ((job->respawn_limit != JOB_DEFAULT_RESPAWN_LIMIT)
		    || (job->respawn_interval != JOB_DEFAULT_RESPAWN_INTERVAL))
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		/* Parse the limit value */
		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->respawn_limit = strtol (arg, &endptr, 10);
		if (*endptr || (job->respawn_limit < 0)) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
		nih_free (arg);

		/* Parse the timeout value */
		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->respawn_interval = strtol (arg, &endptr, 10);
		if (*endptr || (job->respawn_interval < 0)) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * cfg_stanza_service:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a service stanza from @file.  This sets the service flag for the
 * job, and takes no further arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_service (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->service && (! job->respawn))
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->service = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_instance:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an instance stanza from @file, this expects no arguments and
 * simply sets the instance flag in the job.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_instance (Job             *job,
		     NihConfigStanza *stanza,
		     const char      *file,
		     size_t           len,
		     size_t          *pos,
		     size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->instance)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->instance = TRUE;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_pid:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a pid stanza from @file.  This stanza expects an second-level
 * stanza argument indicating which job parameter to set, followed by
 * an argument that sets that.  All are related to discovering the pid
 * of a forked daemon.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_pid (Job             *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "file")) {
		nih_free (arg);

		if (job->pid_file)
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		job->pid_file = nih_config_next_arg (job, file, len,
						     pos, lineno);
		if (! job->pid_file)
			return -1;

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "binary")) {
		nih_free (arg);

		if (job->pid_binary)
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		job->pid_binary = nih_config_next_arg (job, file, len,
						       pos, lineno);
		if (! job->pid_binary)
			return -1;

		return nih_config_skip_comment (file, len, pos, lineno);

	} else if (! strcmp (arg, "timeout")) {
		char *endptr;

		nih_free (arg);

		if (job->pid_timeout != JOB_DEFAULT_PID_TIMEOUT)
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->pid_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->pid_timeout < 0)) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * cfg_stanza_kill:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a kill stanza from @file, extracting a second-level stanza that
 * states which value to set from its argument.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_kill (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "timeout")) {
		char *endptr;

		nih_free (arg);

		if (job->kill_timeout != JOB_DEFAULT_KILL_TIMEOUT)
			nih_return_error (-1, CFG_DUPLICATE_VALUE,
					  _(CFG_DUPLICATE_VALUE_STR));

		arg = nih_config_next_arg (NULL, file, len, pos, lineno);
		if (! arg)
			return -1;

		job->kill_timeout = strtol (arg, &endptr, 10);
		if (*endptr || (job->kill_timeout < 0)) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
		nih_free (arg);

		return nih_config_skip_comment (file, len, pos, lineno);

	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * cfg_stanza_normal:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a normal stanza from @file.  This stanza expects a single "exit"
 * argument, followed by one or more arguments giving signal names or
 * exit codes that the main process can return and be considered to have been
 * stopped normally.
 *
 * Arguments are stored in the normalexit array, and the normalexit_len
 * value updated.  Signals are stored in the higher bytes.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_normal (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_token (NULL, file, len, pos, lineno,
				     NIH_CONFIG_CNLWS, FALSE);
	if (! arg)
		return -1;

	if (! strcmp (arg, "exit")) {
		nih_free (arg);

		do {
			unsigned long  status;
			char          *endptr;
			int           *new_ne, signum;

			arg = nih_config_next_arg (NULL, file, len,
						   pos, lineno);
			if (! arg)
				return -1;

			signum = nih_signal_from_name (arg);
			if (signum < 0) {
				status = strtoul (arg, &endptr, 10);
				if (*endptr || (status > INT_MAX)) {
					nih_free (arg);
					nih_return_error (-1,CFG_ILLEGAL_VALUE,
							  _(CFG_ILLEGAL_VALUE_STR));
				}
			} else {
				status = signum << 8;
			}

			nih_free (arg);

			NIH_MUST (new_ne = nih_realloc (job->normalexit, job,
							sizeof (int) *
							(job->normalexit_len + 1)));

			job->normalexit = new_ne;
			job->normalexit[job->normalexit_len++] = (int) status;
		} while (nih_config_has_token (file, len, pos, lineno));

		return nih_config_skip_comment (file, len, pos, lineno);
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}
}

/**
 * cfg_stanza_console:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a console stanza from @file, extracting a single argument that
 * specifies where console output should be sent.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_console (Job             *job,
		    NihConfigStanza *stanza,
		    const char      *file,
		    size_t           len,
		    size_t          *pos,
		    size_t          *lineno)
{
	char *arg;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	/*
	if (job->console != CONSOLE_LOGGED) {
		nih_free (arg);
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));
	}
	*/

	if (! strcmp (arg, "logged")) {
		job->console = CONSOLE_LOGGED;
	} else if (! strcmp (arg, "output")) {
		job->console = CONSOLE_OUTPUT;
	} else if (! strcmp (arg, "owner")) {
		job->console = CONSOLE_OWNER;
	} else if (! strcmp (arg, "none")) {
		job->console = CONSOLE_NONE;
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_env:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse an env stanza from @file, extracting a single argument of the form
 * VAR=VALUE.  These are stored in the env array, which is increased in
 * size to accomodate the new value.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_env (Job             *job,
		NihConfigStanza *stanza,
		const char      *file,
		size_t           len,
		size_t          *pos,
		size_t          *lineno)
{
	char  *env;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	env = nih_config_next_arg (job->env, file, len, pos, lineno);
	if (! env)
		return -1;

	NIH_MUST (nih_str_array_addp (&job->env, job, NULL, env));

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_umask:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a umask stanza from @file, extracting a single argument containing
 * a process file creation mask.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_umask (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	char          *arg, *endptr;
	unsigned long  mask;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (job->umask != JOB_DEFAULT_UMASK) {
		nih_free (arg);
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));
	}

	mask = strtoul (arg, &endptr, 8);
	if (*endptr || (mask & ~0777)) {
		nih_free (arg);

		nih_return_error (-1, CFG_ILLEGAL_VALUE,
				  _(CFG_ILLEGAL_VALUE_STR));
	}
	nih_free (arg);

	job->umask = (mode_t)mask;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_nice:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a nice stanza from @file, extracting a single argument containing
 * a process priority.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_nice (Job             *job,
		 NihConfigStanza *stanza,
		 const char      *file,
		 size_t           len,
		 size_t          *pos,
		 size_t          *lineno)
{
	char *arg, *endptr;
	long  nice;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (job->nice) {
		nih_free (arg);
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));
	}

	nice = strtol (arg, &endptr, 10);
	if (*endptr || (nice < -20) || (nice > 19)) {
		nih_free (arg);

		nih_return_error (-1, CFG_ILLEGAL_VALUE,
				  _(CFG_ILLEGAL_VALUE_STR));
	}
	nih_free (arg);

	job->nice = (int)nice;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_limit:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a limit stanza from @file, extracting a second-level stanza that
 * states which limit to set from its two following arguments.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_limit (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	int   resource;
	char *arg, *endptr;

	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (! strcmp (arg, "as")) {
		resource = RLIMIT_AS;
	} else if (! strcmp (arg, "core")) {
		resource = RLIMIT_CORE;
	} else if (! strcmp (arg, "cpu")) {
		resource = RLIMIT_CPU;
	} else if (! strcmp (arg, "data")) {
		resource = RLIMIT_DATA;
	} else if (! strcmp (arg, "fsize")) {
		resource = RLIMIT_FSIZE;
	} else if (! strcmp (arg, "memlock")) {
		resource = RLIMIT_MEMLOCK;
	} else if (! strcmp (arg, "msgqueue")) {
		resource = RLIMIT_MSGQUEUE;
	} else if (! strcmp (arg, "nice")) {
		resource = RLIMIT_NICE;
	} else if (! strcmp (arg, "nofile")) {
		resource = RLIMIT_NOFILE;
	} else if (! strcmp (arg, "nproc")) {
		resource = RLIMIT_NPROC;
	} else if (! strcmp (arg, "rss")) {
		resource = RLIMIT_RSS;
	} else if (! strcmp (arg, "rtprio")) {
		resource = RLIMIT_RTPRIO;
	} else if (! strcmp (arg, "sigpending")) {
		resource = RLIMIT_SIGPENDING;
	} else if (! strcmp (arg, "stack")) {
		resource = RLIMIT_STACK;
	} else {
		nih_free (arg);

		nih_return_error (-1, NIH_CONFIG_UNKNOWN_STANZA,
				  _(NIH_CONFIG_UNKNOWN_STANZA_STR));
	}

	nih_free (arg);


	if (job->limits[resource])
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	NIH_MUST (job->limits[resource] = nih_new (job, struct rlimit));


	/* Parse the soft limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (strcmp (arg, "unlimited")) {
		job->limits[resource]->rlim_cur = strtoul (arg, &endptr, 10);
		if (*endptr) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
	} else {
		job->limits[resource]->rlim_cur = RLIM_INFINITY;
	}
	nih_free (arg);

	/* Parse the hard limit value */
	arg = nih_config_next_arg (NULL, file, len, pos, lineno);
	if (! arg)
		return -1;

	if (strcmp (arg, "unlimited")) {
		job->limits[resource]->rlim_max = strtoul (arg, &endptr, 10);
		if (*endptr) {
			nih_free (arg);

			nih_return_error (-1, CFG_ILLEGAL_VALUE,
					  _(CFG_ILLEGAL_VALUE_STR));
		}
	} else {
		job->limits[resource]->rlim_max = RLIM_INFINITY;
	}
	nih_free (arg);

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_chroot:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a chroot stanza from @file, extracting a single argument
 * containing a directory name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_chroot (Job             *job,
		   NihConfigStanza *stanza,
		   const char      *file,
		   size_t           len,
		   size_t          *pos,
		   size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->chroot)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->chroot = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->chroot)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}

/**
 * cfg_stanza_chdir:
 * @job: job being parsed,
 * @stanza: stanza found,
 * @file: file or string to parse,
 * @len: length of @file,
 * @pos: offset within @file,
 * @lineno: line number.
 *
 * Parse a chdir stanza from @file, extracting a single argument
 * containing a directory name.
 *
 * Returns: zero on success, negative value on error.
 **/
static int
cfg_stanza_chdir (Job             *job,
		  NihConfigStanza *stanza,
		  const char      *file,
		  size_t           len,
		  size_t          *pos,
		  size_t          *lineno)
{
	nih_assert (job != NULL);
	nih_assert (stanza != NULL);
	nih_assert (file != NULL);
	nih_assert (pos != NULL);

	if (job->chdir)
		nih_return_error (-1, CFG_DUPLICATE_VALUE,
				  _(CFG_DUPLICATE_VALUE_STR));

	job->chdir = nih_config_next_arg (job, file, len, pos, lineno);
	if (! job->chdir)
		return -1;

	return nih_config_skip_comment (file, len, pos, lineno);
}


/**
 * cfg_read_job:
 * @parent: parent of returned job,
 * @filename: name of file to read,
 * @name: name to call job.
 *
 * Reads the @filename given and uses the information within to construct
 * a new job structure named @name which is returned.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job structure, or NULL if the file was invalid.
 **/
Job *
cfg_read_job (const void *parent,
	      const char *filename,
	      const char *name)
{
	Job    *job, *old_job;
	size_t  lineno;

	nih_assert (filename != NULL);
	nih_assert (name != NULL);

	/* Look for an old job with that name */
	old_job = job_find_by_name (name);

	/* Allocate a new structure */
	NIH_MUST (job = job_new (parent, name));
	nih_debug ("Loading %s from %s", job->name, filename);

	/* Parse the file, if we can't parse the new file, we just return now
	 * without ditching the old job if there is one.
	 */
	lineno = 1;
	if (nih_config_parse (filename, NULL, &lineno, stanzas, job) < 0) {
		NihError *err;

		err = nih_error_get ();
		switch (err->number) {
		case NIH_CONFIG_EXPECTED_TOKEN:
		case NIH_CONFIG_UNEXPECTED_TOKEN:
		case NIH_CONFIG_TRAILING_SLASH:
		case NIH_CONFIG_UNTERMINATED_QUOTE:
		case NIH_CONFIG_UNTERMINATED_BLOCK:
		case NIH_CONFIG_UNKNOWN_STANZA:
		case CFG_ILLEGAL_VALUE:
		case CFG_DUPLICATE_VALUE:
			nih_error ("%s:%zi: %s",
				   filename, lineno, err->message);
			break;
		default:
			nih_error ("%s: %s: %s", filename, _("unable to read"),
				   err->message);
			break;
		}

		nih_list_free (&job->entry);
		nih_free (err);

		return NULL;
	}


	/* Now we sanity check the job, checking for things that will
	 * cause assertions or bad behaviour later on, or just general
	 * warnings
	 */

	/* pid file makes no sense unless daemon */
	if (job->pid_file && (! job->daemon)) {
		nih_warn (_("%s: 'pid file' ignored unless 'daemon' specified"),
			  filename);
	}

	/* pid binary makes no sense unless daemon */
	if (job->pid_binary && (! job->daemon)) {
		nih_warn (_("%s: 'pid binary' ignored unless 'daemon' specified"),
			  filename);
	}


	/* Deal with the case where we're reloading an existing	job; we
	 * mark the existing job as deleted, rather than copying in old data,
	 * since we don't want to mis-match scripts or configuration.
	 */
	if (old_job) {
		nih_info (_("Replacing existing %s job"), job->name);

		if ((old_job->replacement != NULL)
		    && (old_job->replacement != (void *)-1)) {
			nih_debug ("Discarding previous replacement");
			nih_list_free (&old_job->replacement->entry);
		}

		old_job->replacement = job;
		job->replacement_for = old_job;

		if (job_should_replace (old_job))
			job_change_state (old_job, job_next_state (old_job));
	}

	return job;
}


/**
 * cfg_watch_dir:
 * @dirname: directory to watch,
 * @prefix: prefix to apply to job names.
 *
 * Watch @dirname for creation or modification of configuration files or
 * sub-directories and parse them whenever they exist.  This also performs
 * the initial parsing of jobs in the directory.
 *
 * The names of jobs are constructed from @prefix (which may be NULL) and
 * the relative path of the job to @dirname.  If @prefix is not NULL, it
 * would normally end with some separating character such as '/' or '.'
 *
 * The watch can be freed using nih_watch_free().
 *
 * Returns: watch structure on success, NULL if inotify wasn't available
 * and (void *)-1 on raised error.
 **/
NihWatch *
cfg_watch_dir (const char *dirname,
	       const char *prefix)
{
	NihWatch *watch;
	NihError *err;

	nih_assert (dirname != NULL);

	nih_info (_("Reading configuration from %s"), dirname);

	/* We use inotify to keep abreast of any changes to our configuration
	 * directory, parsing them while we walk to add inotify watches.
	 */
	watch = nih_watch_new (NULL, dirname, TRUE, TRUE, nih_file_ignore,
			       (NihCreateHandler)cfg_create_modify_handler,
			       (NihModifyHandler)cfg_create_modify_handler,
			       (NihDeleteHandler)cfg_delete_handler,
			       (void *)prefix);
	if (watch) {
		nih_io_set_cloexec (watch->fd);
		return watch;
	}

	/* Failed to watch with inotify, fall back to walking the directory
	 * the old fashioned way.  If this fails, then there's obviously
	 * some problem with the directory; discard the error nih_watch_new()
	 * returned as it's probably not relevant, and leave the
	 * nih_dir_walk() error raised for our caller.
	 */
	err = nih_error_get ();
	if (nih_dir_walk (dirname, nih_file_ignore,
			  (NihFileVisitor)cfg_visitor, NULL,
			  (void *)prefix) < 0) {
		nih_free (err);
		return (void *)-1;
	}

	/* Walk worked; but inotify didn't ... if this is for any other
	 * reason than inotify simply being not supported, we warn about it.
	 */
	if (err->number != EOPNOTSUPP)
		nih_error ("%s: %s: %s", dirname,
			   _("Unable to watch configuration directory"),
			   err->message);
	nih_free (err);

	return NULL;
}

/**
 * cfg_job_name:
 * @parent: parent for new string,
 * @prefix: prefix to apply to name,
 * @dirname: top-level directory being watched,
 * @path: full path to file.
 *
 * Constructs a job name for a given file by removing @dirname from the
 * front and pre-pending @prefix (which may be NULL).
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: newly allocated job name or NULL if insufficient memory.
 **/
static char *
cfg_job_name (const void *parent,
	      const char *prefix,
	      const char *dirname,
	      const char *path)
{
	nih_assert (dirname != NULL);
	nih_assert (path != NULL);

	/* Remove dirname from the front */
	if (! strncmp (path, dirname, strlen (dirname)))
		path += strlen (dirname);

	/* Remove slashes */
	while (*path == '/')
		path++;

	/* Construct job name */
	if (prefix) {
		return nih_sprintf (parent, "%s%s", prefix, path);
	} else {
		return nih_strdup (parent, path);
	}
}

/**
 * cfg_create_modify_handler:
 * @prefix: prefix to apply to job names,
 * @watch: NihWatch for directory tree,
 * @path: full path to file,
 * @statbuf: stat of @path.
 *
 * This function is called whenever a new job file is created in a directory
 * we're watching, or modified.  We attempt to parse the file as a valid job;
 * though it's pretty common for these to fail since it's probably empty or
 * only partially written.
 **/
static void
cfg_create_modify_handler (const char  *prefix,
			   NihWatch    *watch,
			   const char  *path,
			   struct stat *statbuf)
{
	char *name;

	nih_assert (watch != NULL);
	nih_assert (path != NULL);
	nih_assert (statbuf != NULL);

	if (! S_ISREG (statbuf->st_mode))
		return;

	NIH_MUST (name = cfg_job_name (NULL, prefix, watch->path, path));

	nih_debug ("%s job definition changed", name);

	cfg_read_job (NULL, path, name);

	nih_free (name);
}

/**
 * cfg_delete_handler:
 * @prefix: prefix to apply to job names,
 * @watch: NihWatch for directory tree,
 * @path: full path to file.
 *
 * This function is called whenever a job file is deleted from a directory
 * we're watching.
 **/
static void
cfg_delete_handler (const char *prefix,
		    NihWatch   *watch,
		    const char *path)
{
	Job  *job;
	char *name;

	nih_assert (watch != NULL);
	nih_assert (path != NULL);

	NIH_MUST (name = cfg_job_name (NULL, prefix, watch->path, path));

	nih_debug ("%s job definition deleted", name);

	job = job_find_by_name (name);
	if (job) {
		nih_info (_("Deleting %s job"), job->name);

		if ((job->replacement != NULL)
		    && (job->replacement != (void *)-1)) {
			nih_debug ("Discarding replacement");
			nih_list_free (&job->replacement->entry);
		}

		job->replacement = (void *)-1;

		if (job_should_replace (job))
			job_change_state (job, job_next_state (job));
	}

	nih_free (name);
}

/**
 * cfg_visitor:
 * @prefix: prefix to apply to job names,
 * @dirname: directory being iterated,
 * @path: full path to file,
 * @statbuf: stat of @path.
 *
 * This function is called for each file under a configuration directory
 * whether or not we're watching it for changes; we parse the file to get
 * the initial set of jobs.
 *
 * Returns: always zero.
 **/
static int
cfg_visitor (const char  *prefix,
	     const char  *dirname,
	     const char  *path,
	     struct stat *statbuf)
{
	char *name;

	nih_assert (path != NULL);
	nih_assert (statbuf != NULL);

	if (! S_ISREG (statbuf->st_mode))
		return 0;

	NIH_MUST (name = cfg_job_name (NULL, prefix, dirname, path));

	cfg_read_job (NULL, path, name);

	nih_free (name);

	return 0;
}
