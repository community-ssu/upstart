/* upstart
 *
 * test_cfgfile.c - test suite for init/cfgfile.c
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

#include <nih/test.h>

#ifdef HAVE_SYS_INOTIFY_H
# include <sys/inotify.h>
#else
# include <nih/inotify.h>
#endif /* HAVE_SYS_INOTIFY_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/io.h>
#include <nih/watch.h>
#include <nih/main.h>
#include <nih/logging.h>

#include "cfgfile.h"


/* Macro to aid us in testing the output which includes the program name
 * and filename.
 */
#define TEST_ERROR_EQ(_file, _text) \
	do { \
		char text[512]; \
		sprintf (text, "%s:%s:%s", program_name, filename, (_text)); \
		TEST_FILE_EQ (_file, text); \
	} while (0);


void
test_stanza_exec (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_exec");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an exec stanza sets the process of the job as a single
	 * string.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_MAIN];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/sbin/daemon -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that an exec stanza without any arguments results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "exec /sbin/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	fclose (output);
	unlink (filename);
}

void
test_stanza_script (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_script");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a script stanza begins a block which is stored in
	 * the script member of the job.
	 */
	TEST_FEATURE ("with block");
	jf = fopen (filename, "w");
	fprintf (jf, "script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_MAIN];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "echo\n");

	nih_list_free (&job->entry);


	/* Check that multiple script stanzas result in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that script and exec stanzas result in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a script stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with argument");
	jf = fopen (filename, "w");
	fprintf (jf, "script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_pre_start (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_pre_start");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a pre-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-start exec /bin/tool -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_PRE_START];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that a pre-start exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-start exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the pre-start exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-start exec /bin/tool -d\n");
	fprintf (jf, "pre-start exec /bin/tool\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-start script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_PRE_START];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "echo\n");

	nih_list_free (&job->entry);


	/* Check that multiple pre-start script stanzas result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "pre-start script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-start script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-start script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-start stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-start foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-start stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-start\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_post_start (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_post_start");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a post-start exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	jf = fopen (filename, "w");
	fprintf (jf, "post-start exec /bin/tool -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_POST_START];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that a post-start exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	jf = fopen (filename, "w");
	fprintf (jf, "post-start exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the post-start exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "post-start exec /bin/tool -d\n");
	fprintf (jf, "post-start exec /bin/tool\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-start script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "post-start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_POST_START];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "echo\n");

	nih_list_free (&job->entry);


	/* Check that multiple post-start script stanzas result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "post-start script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "post-start script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-start script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-start script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-start stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-start foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-start stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-start\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_pre_stop (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_pre_stop");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a pre-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-stop exec /bin/tool -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_PRE_STOP];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that a pre-stop exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-stop exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the pre-stop exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-stop exec /bin/tool -d\n");
	fprintf (jf, "pre-stop exec /bin/tool\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-stop script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_PRE_STOP];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "echo\n");

	nih_list_free (&job->entry);


	/* Check that multiple pre-stop script stanzas result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "pre-stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "pre-stop script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-stop script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-stop script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-stop stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-stop foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pre-stop stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "pre-stop\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_post_stop (void)
{
	Job        *job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_post_stop");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a post-stop exec stanza sets the process of the
	 * job as a single string.
	 */
	TEST_FEATURE ("with exec and command");
	jf = fopen (filename, "w");
	fprintf (jf, "post-stop exec /bin/tool -d \"foo\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_POST_STOP];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/bin/tool -d \"foo\"");

	nih_list_free (&job->entry);


	/* Check that a post-stop exec stanza without any arguments results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with exec but no command");
	jf = fopen (filename, "w");
	fprintf (jf, "post-stop exec\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "1: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the post-stop exec stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with duplicates");
	jf = fopen (filename, "w");
	fprintf (jf, "post-stop exec /bin/tool -d\n");
	fprintf (jf, "post-stop exec /bin/tool\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-stop script stanza begins a block which
	 * is stored in the process.
	 */
	TEST_FEATURE ("with script and block");
	jf = fopen (filename, "w");
	fprintf (jf, "post-stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	process = job->process[PROCESS_POST_STOP];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "echo\n");

	nih_list_free (&job->entry);


	/* Check that multiple post-stop script stanzas result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with multiple blocks");
	jf = fopen (filename, "w");
	fprintf (jf, "post-stop script\n");
	fprintf (jf, "    echo\n");
	fprintf (jf, "end script\n");
	fprintf (jf, "post-stop script\n");
	fprintf (jf, "    ls\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-stop script stanza with an extra argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with argument to script");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-stop script foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-stop stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-stop foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a post-stop stanza with no second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "post-stop\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}


void
test_stanza_start (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_start");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a start stanza with an on argument followed by an
	 * event name results in the named event being added to the
	 * start events list.
	 */
	TEST_FEATURE ("with on and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that all arguments to the start on stanza are consumed,
	 * with additional arguments after the first being treated as
	 * arguments for the event.
	 */
	TEST_FEATURE ("with on and multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on wibble foo bar b?z*\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	TEST_ALLOC_PARENT (event->args, event);
	TEST_ALLOC_SIZE (event->args, sizeof (char *) * 4);
	TEST_ALLOC_PARENT (event->args[0], event->args);
	TEST_ALLOC_PARENT (event->args[1], event->args);
	TEST_ALLOC_PARENT (event->args[2], event->args);
	TEST_EQ_STR (event->args[0], "foo");
	TEST_EQ_STR (event->args[1], "bar");
	TEST_EQ_STR (event->args[2], "b?z*");
	TEST_EQ_P (event->args[3], NULL);

	nih_list_free (&job->entry);


	/* Check that repeated start on stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple on stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on wibble\n");
	fprintf (jf, "start on wobble\n");
	fprintf (jf, "start on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->start_events);

	event = (Event *)job->start_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that a start stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a start on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "start on\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_stop (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_stop");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a stop stanza with an on argument followed by an
	 * event name results in the named event being added to the
	 * stop events list.
	 */
	TEST_FEATURE ("with on and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->stop_events);

	event = (Event *)job->stop_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that all arguments to the stop on stanza are consumed,
	 * with additional arguments after the first being treated as
	 * arguments for the event.
	 */
	TEST_FEATURE ("with on and multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on wibble foo bar b?z*\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->stop_events);

	event = (Event *)job->stop_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	TEST_ALLOC_PARENT (event->args, event);
	TEST_ALLOC_SIZE (event->args, sizeof (char *) * 4);
	TEST_ALLOC_PARENT (event->args[0], event->args);
	TEST_ALLOC_PARENT (event->args[1], event->args);
	TEST_ALLOC_PARENT (event->args[2], event->args);
	TEST_EQ_STR (event->args[0], "foo");
	TEST_EQ_STR (event->args[1], "bar");
	TEST_EQ_STR (event->args[2], "b?z*");
	TEST_EQ_P (event->args[3], NULL);

	nih_list_free (&job->entry);


	/* Check that repeated stop on stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple on stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on wibble\n");
	fprintf (jf, "stop on wobble\n");
	fprintf (jf, "stop on waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->stop_events);

	event = (Event *)job->stop_events.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that a stop stanza without a second-level argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop stanza with an unknown second-level argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a stop on stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with on and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "stop on\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}


void
test_stanza_description (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_description");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a description stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->description, job);
	TEST_EQ_STR (job->description, "a test job");

	nih_list_free (&job->entry);


	/* Check that a description stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a description stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated description stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "description \"a test job\"\n");
	fprintf (jf, "description \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_author (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_author");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a author stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->author, job);
	TEST_EQ_STR (job->author, "a test job");

	nih_list_free (&job->entry);


	/* Check that a author stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a author stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated author stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "author \"a test job\"\n");
	fprintf (jf, "author \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_version (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_version");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a version stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\"\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->version, job);
	TEST_EQ_STR (job->version, "a test job");

	nih_list_free (&job->entry);


	/* Check that a version stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a version stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\" \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated version stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "version \"a test job\"\n");
	fprintf (jf, "version \"ya ya\"\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_emits (void)
{
	Job   *job;
	Event *event;
	FILE  *jf, *output;
	char   filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_emits");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an emits stanza with a single argument results in
	 * the named event being added to the emits list.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	nih_list_free (&job->entry);


	/* Check that an emits stanza with multiple arguments results in
	 * all of the named events being added to the emits list.
	 */
	TEST_FEATURE ("with multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble wobble waggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	nih_list_free (&job->entry);


	/* Check that repeated emits stanzas are permitted, each appending
	 * to the last.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits wibble\n");
	fprintf (jf, "emits wobble waggle\n");
	fprintf (jf, "emits wuggle\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_NOT_EMPTY (&job->emits);

	event = (Event *)job->emits.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wibble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wobble");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "waggle");

	event = (Event *)event->entry.next;
	TEST_ALLOC_SIZE (event, sizeof (Event));
	TEST_EQ_STR (event->name, "wuggle");

	nih_list_free (&job->entry);


	/* Check that an emits stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "emits\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}


void
test_stanza_daemon (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_daemon");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a daemon stanza without any arguments sets the job's
	 * daemon flag.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->daemon);

	nih_list_free (&job->entry);


	/* Check that a daemon stanza with arguments results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the daemon stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with duplicate");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_respawn (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_respawn");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a respawn stanza sets the job's respawn and service */
	TEST_FEATURE ("with no argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->respawn);
	TEST_TRUE (job->service);

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with the limit argument and numeric
	 * rate and timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with limit and two arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10 120\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->respawn_limit, 10);
	TEST_EQ (job->respawn_interval, 120);

	nih_list_free (&job->entry);


	/* Check that a respawn stanza with the limit argument but no
	 * interval results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn stanza with the limit argument but no
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with limit and missing arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a non-integer interval
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a non-integer limit
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and non-integer limit argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit foo 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a partially numeric
	 * interval argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a partially numeric
	 * limit argument results in a syntax error.
	 */
	TEST_FEATURE ("with limit and alphanumeric limit argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 99foo 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a negative interval
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10 -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with a negative limit
	 * value results in a syntax error.
	 */
	TEST_FEATURE ("with limit and negative interval argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit -1 120\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the respawn stanza without
	 * arguments results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate without arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate respawn limit stanzas results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with duplicate limit stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 10 120\n");
	fprintf (jf, "respawn limit 20 90\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn limit stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument to limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn limit 0 1 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a respawn stanza with an unknown second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument to limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn foo bar\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_service (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_service");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a service stanza without any arguments sets the job's
	 * service flag.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "service\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->service);

	nih_list_free (&job->entry);


	/* Check that a service stanza with arguments results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "service foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the service stanza results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with duplicate");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "service\n");
	fprintf (jf, "service\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that we can specify the service and respawn stanzas.
	 */
	TEST_FEATURE ("with no arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fprintf (jf, "service\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->respawn);
	TEST_TRUE (job->service);

	nih_list_free (&job->entry);


	fclose (output);
	unlink (filename);
}

void
test_stanza_instance (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_instance");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that an instance stanza sets the job's instance flag.
	 */
	TEST_FEATURE ("with no argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_TRUE (job->instance);

	nih_list_free (&job->entry);


	/* Check that any arguments to the instance stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the instance stanza result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with duplicate");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "instance\n");
	fprintf (jf, "instance\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	fclose (output);
	unlink (filename);
}

void
test_stanza_pid (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_pid");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a pid stanza with the file argument and a filename
	 * results in the filename being stored in the job.
	 */
	TEST_FEATURE ("with file and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid file /var/run/daemon.pid\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->pid_file, job);
	TEST_EQ_STR (job->pid_file, "/var/run/daemon.pid");

	nih_list_free (&job->entry);


	/* Check that a pid stanza with the binary argument and a filename
	 * results in the filename being stored in the job.
	 */
	TEST_FEATURE ("with binary and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->pid_binary, job);
	TEST_EQ_STR (job->pid_binary, "/usr/lib/daemon");

	nih_list_free (&job->entry);


	/* Check that a pid stanza with the timeout argument and a numeric
	 * timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with timeout and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout 10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->pid_timeout, 10);

	nih_list_free (&job->entry);


	/* Check that a pid stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the file argument but no filename
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with file and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid file\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the binary argument but no filename
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with binary and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid binary\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid timeout stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the file argument and filename,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with file and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid file /var/run/daemon.pid foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the binary argument and filename,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with binary and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a pid stanza with the timeout argument and timeout,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with timeout and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout 99 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid file stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate file stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid file /var/run/daemon.pid\n");
	fprintf (jf, "pid file /var/run/daemon/daemon.pid\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid binary stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate binary stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon\n");
	fprintf (jf, "pid binary /usr/lib/daemon/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated pid timeout stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate timeout stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "daemon\n");
	fprintf (jf, "pid timeout 99\n");
	fprintf (jf, "pid timeout 100\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "4: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_kill (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_kill");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a kill stanza with the timeout argument and a numeric
	 * timeout results in it being stored in the job.
	 */
	TEST_FEATURE ("with timeout and single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->kill_timeout, 10);

	nih_list_free (&job->entry);


	/* Check that a kill stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with an invalid second-level stanza
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown second argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with the timeout argument but no timeout
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a non-integer argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with timeout and alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill timeout stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with timeout and negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a kill stanza with the timeout argument and timeout,
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with timeout and extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated kill timeout stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate timeout stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "kill timeout 99\n");
	fprintf (jf, "kill timeout 100\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_normal (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_normal");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a normal exit stanza with a single argument results in
	 * the exit code given being added to the normalexit array, which
	 * should be allocated.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit 99\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 1);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);

	nih_list_free (&job->entry);


	/* Check that an argument in a normal exit stanza may be a signal name,
	 * in which case the signal number is shifted left and then added
	 * to the normalexit array.
	 */
	TEST_FEATURE ("with single argument containing signal name");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit INT\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 1);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], SIGINT << 8);

	nih_list_free (&job->entry);


	/* Check that a normal exit stanza with multiple arguments results in
	 * all of the given exit codes being added to the array, which should
	 * have been increased in size.
	 */
	TEST_FEATURE ("with multiple arguments");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit 99 100 101 SIGTERM\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 4);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);
	TEST_EQ (job->normalexit[1], 100);
	TEST_EQ (job->normalexit[2], 101);
	TEST_EQ (job->normalexit[3], SIGTERM << 8);

	nih_list_free (&job->entry);


	/* Check that repeated normal exit stanzas are permitted, each
	 * appending to the array.
	 */
	TEST_FEATURE ("with multiple stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit 99\n");
	fprintf (jf, "normal exit 100 101\n");
	fprintf (jf, "normal exit QUIT\n");
	fprintf (jf, "normal exit 900\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->normalexit_len, 5);
	TEST_ALLOC_SIZE (job->normalexit, sizeof (int) * job->normalexit_len);
	TEST_ALLOC_PARENT (job->normalexit, job);

	TEST_EQ (job->normalexit[0], 99);
	TEST_EQ (job->normalexit[1], 100);
	TEST_EQ (job->normalexit[2], 101);
	TEST_EQ (job->normalexit[3], SIGQUIT << 8);
	TEST_EQ (job->normalexit[4], 900);

	nih_list_free (&job->entry);


	/* Check that a normal exit stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normal exit stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normal exit stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normal exit stanza with a negative value results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal exit -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normal stanza with something other than "exit"
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal wibble\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a normal stanza without an argument results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing exit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "normal\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_console (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_console");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);

	/* Check that console logged sets the job's console to
	 * CONSOLE_LOGGED.
	 */
	TEST_FEATURE ("with logged argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console logged\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->console, CONSOLE_LOGGED);

	nih_list_free (&job->entry);


	/* Check that console output sets the job's console to
	 * CONSOLE_OUTPUT.
	 */
	TEST_FEATURE ("with output argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console output\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->console, CONSOLE_OUTPUT);

	nih_list_free (&job->entry);


	/* Check that console owner sets the job's console to
	 * CONSOLE_OWNER.
	 */
	TEST_FEATURE ("with owner argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console owner\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->console, CONSOLE_OWNER);

	nih_list_free (&job->entry);


	/* Check that console none sets the job's console to
	 * CONSOLE_NONE.
	 */
	TEST_FEATURE ("with none argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console none\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->console, CONSOLE_NONE);

	nih_list_free (&job->entry);


	/* Check that an unknown argument raises a syntax error.
	 */
	TEST_FEATURE ("with unknown argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console wibble\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that additional arguments to the stanza results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console owner foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


#if 0
	/* Check that duplicate occurances of the console stanza result
	 * in a syntax error.
	 */
	TEST_FEATURE ("with duplicate");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "console owner\n");
	fprintf (jf, "console output\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);
#endif


	fclose (output);
	unlink (filename);
}

void
test_stanza_env (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_env");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a env stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "env FOO=BAR\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->env, job);
	TEST_ALLOC_SIZE (job->env, sizeof (char *) * 2);
	TEST_EQ_STR (job->env[0], "FOO=BAR");
	TEST_EQ_P (job->env[1], NULL);

	nih_list_free (&job->entry);


	/* Check that repeated env stanzas are appended to those stored in
	 * the job.
	 */
	TEST_FEATURE ("with repeated stanzas");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "env FOO=BAR\n");
	fprintf (jf, "env BAZ=QUUX\n");
	fprintf (jf, "env FRODO=BILBO\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->env, job);
	TEST_ALLOC_SIZE (job->env, sizeof (char *) * 4);
	TEST_EQ_STR (job->env[0], "FOO=BAR");
	TEST_EQ_STR (job->env[1], "BAZ=QUUX");
	TEST_EQ_STR (job->env[2], "FRODO=BILBO");
	TEST_EQ_P (job->env[3], NULL);

	nih_list_free (&job->entry);


	/* Check that a env stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "env\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a env stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "env FOO=BAR oops\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_umask (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_umask");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a umask stanza with an octal timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask 0755\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->umask, 0755);

	nih_list_free (&job->entry);


	/* Check that a umask stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a umask stanza with a non-octal argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-octal argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask 999\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a umask stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a umask stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a umask stanza with a negative value results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with negative argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask -1\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a umask stanza with a creation mask
	 * but with an extra argument afterwards results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask 0755 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated umask stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "umask 0755\n");
	fprintf (jf, "umask 0711\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_nice (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_nice");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a nice stanza with an positive timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with positive argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice 10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->nice, 10);

	nih_list_free (&job->entry);


	/* Check that a nice stanza with a negative timeout results
	 * in it being stored in the job.
	 */
	TEST_FEATURE ("with positive argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice -10\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_EQ (job->nice, -10);

	nih_list_free (&job->entry);


	/* Check that a nice stanza without an argument results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a nice stanza with an overly large argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly large argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice 20\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a nice stanza with an overly small argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with overly small argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice -21\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a nice stanza with a non-integer argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with non-integer argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a nice stanza with a partially numeric argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice 12foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a nice stanza with a priority but with an extra
	 * argument afterwards results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice 10 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated nice stanza results in a syntax error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "nice 10\n");
	fprintf (jf, "nice -12\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_limit (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_limit");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that the limit as stanza sets the RLIMIT_AS resource.
	 */
	TEST_FEATURE ("with as limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit as 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_AS], job);
	TEST_EQ (job->limits[RLIMIT_AS]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_AS]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit core stanza sets the RLIMIT_CORE resource.
	 */
	TEST_FEATURE ("with core limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit as stanza sets the RLIMIT_CPU resource.
	 */
	TEST_FEATURE ("with cpu limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit cpu 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit data stanza sets the RLIMIT_DATA resource.
	 */
	TEST_FEATURE ("with data limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit data 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_DATA], job);
	TEST_EQ (job->limits[RLIMIT_DATA]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_DATA]->rlim_max, 20);

	nih_list_free (&job->entry);

	/* Check that the limit fsize stanza sets the RLIMIT_FSIZE resource.
	 */
	TEST_FEATURE ("with fsize limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit fsize 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_FSIZE], job);
	TEST_EQ (job->limits[RLIMIT_FSIZE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_FSIZE]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit memlock stanza sets the RLIMIT_MEMLOCK
	 * resource.
	 */
	TEST_FEATURE ("with memlock limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit memlock 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_MEMLOCK], job);
	TEST_EQ (job->limits[RLIMIT_MEMLOCK]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_MEMLOCK]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit msgqueue stanza sets the RLIMIT_MSGQUEUE
	 * resource.
	 */
	TEST_FEATURE ("with msgqueue limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit msgqueue 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_MSGQUEUE], job);
	TEST_EQ (job->limits[RLIMIT_MSGQUEUE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_MSGQUEUE]->rlim_max, 20);

	nih_list_free (&job->entry);

	/* Check that the limit nice stanza sets the RLIMIT_NICE resource.
	 */
	TEST_FEATURE ("with nice limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit nice 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_NICE], job);
	TEST_EQ (job->limits[RLIMIT_NICE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_NICE]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit nofile stanza sets the RLIMIT_NOFILE
	 * resource.
	 */
	TEST_FEATURE ("with nofile limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit nofile 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_NOFILE], job);
	TEST_EQ (job->limits[RLIMIT_NOFILE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_NOFILE]->rlim_max, 20);

	nih_list_free (&job->entry);

	/* Check that the limit nproc stanza sets the RLIMIT_NPROC resource.
	 */
	TEST_FEATURE ("with nproc limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit nproc 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_NPROC], job);
	TEST_EQ (job->limits[RLIMIT_NPROC]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_NPROC]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit rss stanza sets the RLIMIT_RSS resource.
	 */
	TEST_FEATURE ("with rss limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit rss 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_RSS], job);
	TEST_EQ (job->limits[RLIMIT_RSS]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_RSS]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit rtprio stanza sets the RLIMIT_RTPRIO resource.
	 */
	TEST_FEATURE ("with rtprio limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit rtprio 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_RTPRIO], job);
	TEST_EQ (job->limits[RLIMIT_RTPRIO]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_RTPRIO]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit sigpending stanza sets the RLIMIT_SIGPENDING
	 * resource.
	 */
	TEST_FEATURE ("with sigpending limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit sigpending 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_SIGPENDING], job);
	TEST_EQ (job->limits[RLIMIT_SIGPENDING]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_SIGPENDING]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that the limit stack stanza sets the RLIMIT_STACK resource.
	 */
	TEST_FEATURE ("with stack limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit stack 10 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_STACK], job);
	TEST_EQ (job->limits[RLIMIT_STACK]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_STACK]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that multiple limit stanzas are permitted provided they
	 * refer to different resources, all are set.
	 */
	TEST_FEATURE ("with multiple limits");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 20\n");
	fprintf (jf, "limit cpu 15 30\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CPU], job);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_cur, 15);
	TEST_EQ (job->limits[RLIMIT_CPU]->rlim_max, 30);

	nih_list_free (&job->entry);


	/* Check that the hard resource limit can be set to unlimited with
	 * a special argument of that name
	 */
	TEST_FEATURE ("with unlimited hard limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 unlimited\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, 10);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, RLIM_INFINITY);

	nih_list_free (&job->entry);


	/* Check that the soft resource limit can be set to unlimited with
	 * a special argument of that name
	 */
	TEST_FEATURE ("with unlimited soft limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core unlimited 20\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->limits[RLIMIT_CORE], job);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_cur, RLIM_INFINITY);
	TEST_EQ (job->limits[RLIMIT_CORE]->rlim_max, 20);

	nih_list_free (&job->entry);


	/* Check that a limit stanza with the soft argument but no hard value
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with missing hard limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with no soft value results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing soft limit");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with an unknown resource name results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with unknown resource type");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unknown stanza\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with no resource name results in a
	 * syntax error.
	 */
	TEST_FEATURE ("with missing resource type");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with a non-integer hard value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with non-integer hard value argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with a non-integer soft value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with non-integer soft value argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core foo 20\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with a partially numeric hard value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric hard value argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 99foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with a partially numeric soft value
	 * argument results in a syntax error.
	 */
	TEST_FEATURE ("with alphanumeric soft value argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 99foo 20\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Illegal value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that duplicate occurances of the limit stanza with the
	 * same reousrce is a syntax error.
	 */
	TEST_FEATURE ("with duplicate resource");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit core 10 20\n");
	fprintf (jf, "limit core 15 30\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a limit stanza with an extra argument results
	 * in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "limit cpu 10 20 foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_chroot (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_chroot");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a chroot stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chroot /chroot/daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->chroot, job);
	TEST_EQ_STR (job->chroot, "/chroot/daemon");

	nih_list_free (&job->entry);


	/* Check that a chroot stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chroot\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a chroot stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chroot /chroot/daemon foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated chroot stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chroot /chroot/daemon\n");
	fprintf (jf, "chroot /var/lib/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}

void
test_stanza_chdir (void)
{
	Job  *job;
	FILE *jf, *output;
	char  filename[PATH_MAX];

	TEST_FUNCTION ("cfg_stanza_chdir");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a chdir stanza with an argument results in it
	 * being stored in the job.
	 */
	TEST_FEATURE ("with single argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chdir /var/lib/daemon\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));

	TEST_ALLOC_PARENT (job->chdir, job);
	TEST_EQ_STR (job->chdir, "/var/lib/daemon");

	nih_list_free (&job->entry);


	/* Check that a chdir stanza without an argument results in
	 * a syntax error.
	 */
	TEST_FEATURE ("with missing argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chdir\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Expected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a chdir stanza with an extra second argument
	 * results in a syntax error.
	 */
	TEST_FEATURE ("with extra argument");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chdir /var/lib/daemon foo\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "2: Unexpected token\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	/* Check that a repeated chdir stanza results in a syntax
	 * error.
	 */
	TEST_FEATURE ("with duplicate stanza");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "chdir /var/lib/daemon\n");
	fprintf (jf, "chdir /var/run/daemon\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, "3: Duplicate value\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);


	fclose (output);
	unlink (filename);
}


static int destructor_called = 0;

static int
my_destructor (void *ptr)
{
	destructor_called++;
	return 0;
}

void
test_read_job (void)
{
	Job        *job, *new_job;
	JobProcess *process;
	FILE       *jf, *output;
	char        filename[PATH_MAX];

	TEST_FUNCTION ("cfg_read_job");
	program_name = "test";
	output = tmpfile ();

	TEST_FILENAME (filename);


	/* Check that a simple job file can be parsed, with all of the
	 * information given filled into the job structure.
	 */
	TEST_FEATURE ("with simple job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon -d\n");
	fprintf (jf, "pre-start script\n");
	fprintf (jf, "    rm /var/lock/daemon\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	process = job->process[PROCESS_MAIN];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/sbin/daemon -d");

	process = job->process[PROCESS_PRE_START];
	TEST_ALLOC_PARENT (process, job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, TRUE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "rm /var/lock/daemon\n");


	/* Check that when we give a new file for an existing job, the
	 * existing job is marked for replacement (and the previous
	 * replacement discarded), but as that job is running, left to
	 * stop on its own later.
	 */
	TEST_FEATURE ("with re-reading existing job file");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon --daemon\n");
	fclose (jf);

	job->goal = JOB_START;
	job->state = JOB_RUNNING;
	job->process[PROCESS_MAIN]->pid = 1000;

	job->replacement = job_new (NULL, "wibble");

	destructor_called = 0;
	nih_alloc_set_destructor (job->replacement, my_destructor);

	new_job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (new_job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	process = new_job->process[PROCESS_MAIN];
	TEST_ALLOC_PARENT (process, new_job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/sbin/daemon --daemon");

	TEST_EQ (job->goal, JOB_START);
	TEST_EQ (job->state, JOB_RUNNING);
	TEST_EQ (job->process[PROCESS_MAIN]->pid, 1000);

	TEST_EQ (new_job->goal, JOB_STOP);
	TEST_EQ (new_job->state, JOB_WAITING);
	TEST_EQ (new_job->process[PROCESS_MAIN]->pid, 0);

	TEST_TRUE (destructor_called);

	TEST_EQ_P (job->replacement, new_job);
	TEST_EQ_P (new_job->replacement_for, job);

	nih_list_free (&job->entry);

	job = new_job;
	job->replacement_for = NULL;


	/* Check that a stopped job can be instantly replaced and marked for
	 * deletion if it's waiting.
	 */
	TEST_FEATURE ("with re-reading stopped job");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon --daemon\n");
	fclose (jf);

	job->goal = JOB_STOP;
	job->state = JOB_WAITING;
	job->process[PROCESS_MAIN]->pid = 0;

	new_job = cfg_read_job (NULL, filename, "test");

	TEST_ALLOC_SIZE (new_job, sizeof (Job));
	TEST_LIST_EMPTY (&job->start_events);
	TEST_LIST_EMPTY (&job->stop_events);

	process = new_job->process[PROCESS_MAIN];
	TEST_ALLOC_PARENT (process, new_job->process);
	TEST_ALLOC_SIZE (process, sizeof (JobProcess));
	TEST_EQ (process->script, FALSE);
	TEST_ALLOC_PARENT (process->command, process);
	TEST_EQ_STR (process->command, "/sbin/daemon --daemon");

	TEST_EQ (job->goal, JOB_STOP);
	TEST_EQ (job->state, JOB_DELETED);

	TEST_EQ (new_job->goal, JOB_STOP);
	TEST_EQ (new_job->state, JOB_WAITING);
	TEST_EQ (new_job->process[PROCESS_MAIN]->pid, 0);

	TEST_EQ_P (job->replacement, new_job);
	TEST_EQ_P (new_job->replacement_for, NULL);

	nih_list_free (&new_job->entry);
	nih_list_free (&job->entry);


	/* Check that a job may have both exec and script missing.
	 */
	TEST_FEATURE ("with missing exec and script");
	jf = fopen (filename, "w");
	fprintf (jf, "description state");
	fclose (jf);

	job = cfg_read_job (NULL, filename, "test");
	rewind (output);

	TEST_ALLOC_SIZE (job, sizeof (Job));
	TEST_EQ_P (job->process[PROCESS_MAIN], NULL);


	/* Check that a job may not use options that normally affect daemon
	 * if it doesn't use daemon itself.  It gets warnings.
	 */
	TEST_FEATURE ("with daemon options and not daemon");
	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/foo\n");
	fprintf (jf, "pid file /var/run/foo.pid\n");
	fprintf (jf, "pid binary /lib/foo/foo.bin\n");
	fclose (jf);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_ERROR_EQ (output,
		       " 'pid file' ignored unless 'daemon' specified\n");
	TEST_ERROR_EQ (output,
		       " 'pid binary' ignored unless 'daemon' specified\n");
	TEST_FILE_END (output);

	TEST_FILE_RESET (output);

	nih_list_free (&job->entry);


	/* Check that a non-existant file is caught properly. */
	TEST_FEATURE ("with non-existant file");
	unlink (filename);

	TEST_DIVERT_STDERR (output) {
		job = cfg_read_job (NULL, filename, "test");
	}
	rewind (output);

	TEST_EQ_P (job, NULL);

	TEST_ERROR_EQ (output, " unable to read: No such file or directory\n");
	TEST_FILE_END (output);


	fclose (output);
	unlink (filename);
}


static int logger_called = 0;
static char *last_message = NULL;

static int
my_logger (NihLogLevel  priority,
	   const char  *message)
{
	logger_called++;
	last_message = strdup (message);

	return 0;
}

void
test_watch_dir (void)
{
	NihWatch *watch;
	Job      *job, *old_job;
	FILE     *jf;
	fd_set    readfds, writefds, exceptfds;
	char      dirname[PATH_MAX], filename[PATH_MAX];
	int       fd[4096], i, nfds;

	TEST_FUNCTION ("cfg_watch_dir");
	TEST_FILENAME (dirname);
	mkdir (dirname, 0755);

	/* Make sure we have inotify before performing these tests... */
	fd[0] = inotify_init ();
	if (fd[0] < 0) {
		printf ("SKIP: inotify not available\n");
		goto no_inotify;
	}
	close (fd[0]);

	strcpy (filename, dirname);
	strcat (filename, "/foo");

	jf = fopen (filename, "w");
	fprintf (jf, "exec /sbin/daemon\n");
	fprintf (jf, "respawn\n");
	fclose (jf);

	strcpy (filename, dirname);
	strcat (filename, "/bar");

	jf = fopen (filename, "w");
	fprintf (jf, "script\n");
	fprintf (jf, "  echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	strcpy (filename, dirname);
	strcat (filename, "/frodo");

	mkdir (filename, 0755);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/foo");

	jf = fopen (filename, "w");
	fprintf (jf, "exec /bin/tool\n");
	fclose (jf);


	/* Check that we can watch a configuration directory with inotify,
	 * and that any existing job definitions are parsed.  The watch
	 * file descriptor should be marked close-on-exec.
	 */
	TEST_FEATURE ("with new watch");
	watch = cfg_watch_dir (dirname, NULL);

	TEST_ALLOC_SIZE (watch, sizeof (NihWatch));
	TEST_EQ_STR (watch->path, dirname);
	TEST_EQ_P (watch->data, NULL);

	TEST_TRUE (fcntl (watch->fd, F_GETFD) & FD_CLOEXEC);

	job = job_find_by_name ("foo");

	TEST_NE_P (job, NULL);
	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon");

	nih_list_free (&job->entry);

	job = job_find_by_name ("bar");

	TEST_NE_P (job, NULL);
	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);

	job = job_find_by_name ("frodo/foo");

	TEST_NE_P (job, NULL);
	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/bin/tool");

	nih_list_free (&job->entry);


	/* Check that we can create a new job file, and that it is
	 * automatically parsed and loaded.
	 */
	TEST_FEATURE ("with new job");
	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	jf = fopen (filename, "w");
	fprintf (jf, "respawn\n");
	fprintf (jf, "script\n");
	fprintf (jf, "  echo\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	job = job_find_by_name ("frodo/bar");

	TEST_NE_P (job, NULL);
	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");


	/* Check that we can modify the existing job entry, and that it is
	 * automatically reparsed with the previous structure being marked
	 * as deleted.
	 */
	TEST_FEATURE ("with modification to job");
	old_job = job;

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	jf = fopen (filename, "w");
	fprintf (jf, "respawn\n");
	fprintf (jf, "script\n");
	fprintf (jf, "  sleep 5\n");
	fprintf (jf, "end script\n");
	fclose (jf);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	job = job_find_by_name ("frodo/bar");

	TEST_EQ_P (old_job->replacement, job);
	TEST_EQ_P (job->replacement_for, NULL);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	nih_list_free (&old_job->entry);

	TEST_NE_P (job, NULL);
	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "sleep 5\n");


	/* Check that when a configuration file is deleted, the job is also
	 * marked for deletion with any previous replacement discarded.
	 */
	TEST_FEATURE ("with deletion of job");
	old_job = job;
	old_job->replacement = job_new (NULL, "wibble");

	destructor_called = 0;
	nih_alloc_set_destructor (old_job->replacement, my_destructor);

	strcpy (filename, dirname);
	strcat (filename, "/frodo/bar");

	unlink (filename);

	nfds = 0;
	FD_ZERO (&readfds);
	FD_ZERO (&writefds);
	FD_ZERO (&exceptfds);

	nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);
	nih_io_handle_fds (&readfds, &writefds, &exceptfds);

	TEST_TRUE (destructor_called);

	TEST_EQ_P (old_job->replacement, (void *)-1);
	TEST_EQ (old_job->goal, JOB_STOP);
	TEST_EQ (old_job->state, JOB_DELETED);

	nih_list_free (&old_job->entry);

	job = job_find_by_name ("frodo/bar");

	TEST_EQ_P (job, NULL);


	nih_watch_free (watch);


	/* Consume all available inotify instances so that the tests run
	 * without inotify.
	 */
	for (i = 0; i < 4096; i++)
		if ((fd[i] = inotify_init ()) < 0)
			break;
no_inotify:

	/* Check that if we don't have inotify, or just don't have any
	 * available watches, we simply iterate the directory once and
	 * parse everything that's there.  Make sure jobs have the prefix
	 * we give appended, and if we only exhausted the inotify
	 * descriptors, a warning output explaining that this has happened.
	 */
	TEST_FEATURE ("with iteration only");
	logger_called = 0;
	last_message = NULL;
	nih_log_set_logger (my_logger);

	watch = cfg_watch_dir (dirname, "test-");

	TEST_EQ_P (watch, NULL);

	if (i) {
		TEST_TRUE (logger_called);
		TEST_EQ_STR (strchr (last_message, ':'),
			     (": Unable to watch configuration directory"
			      ": Too many open files"));

		free (last_message);
	} else {
		TEST_FALSE (logger_called);
	}

	job = job_find_by_name ("test-foo");

	TEST_NE_P (job, NULL);
	TEST_TRUE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/sbin/daemon");

	nih_list_free (&job->entry);

	job = job_find_by_name ("test-bar");

	TEST_NE_P (job, NULL);
	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, TRUE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command, "echo\n");

	nih_list_free (&job->entry);

	job = job_find_by_name ("test-frodo/foo");

	TEST_NE_P (job, NULL);
	TEST_FALSE (job->respawn);
	TEST_NE_P (job->process[PROCESS_MAIN], NULL);
	TEST_EQ (job->process[PROCESS_MAIN]->script, FALSE);
	TEST_EQ_STR (job->process[PROCESS_MAIN]->command,
		     "/bin/tool");

	nih_list_free (&job->entry);


	/* Release consumed instances */
	for (i = 0; i < 4096; i++) {
		if (fd[i] < 0)
			break;

		close (fd[i]);
	}
}


int
main (int   argc,
      char *argv[])
{
	test_stanza_exec ();
	test_stanza_script ();
	test_stanza_pre_start ();
	test_stanza_post_start ();
	test_stanza_pre_stop ();
	test_stanza_post_stop ();
	test_stanza_start ();
	test_stanza_stop ();
	test_stanza_description ();
	test_stanza_version ();
	test_stanza_author ();
	test_stanza_emits ();
	test_stanza_daemon ();
	test_stanza_respawn ();
	test_stanza_service ();
	test_stanza_instance ();
	test_stanza_pid ();
	test_stanza_kill ();
	test_stanza_normal ();
	test_stanza_console ();
	test_stanza_env ();
	test_stanza_umask ();
	test_stanza_nice ();
	test_stanza_limit ();
	test_stanza_chroot ();
	test_stanza_chdir ();
	test_read_job ();
	test_watch_dir ();

	return 0;
}
