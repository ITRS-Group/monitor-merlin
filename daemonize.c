#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>

#include "daemonize.h"

static const char *daemon_pidfile;

/*
 * Really stupid generic sighandler...
 */
static void sighandler(int sig)
{
	unlink(daemon_pidfile);

	exit(EXIT_SUCCESS);
}


/*
 * Read a pid from "pidfile", which must contain one pid
 * and one pid only
 */
static int read_pid(const char *pidfile)
{
	int pid = 0, fd;
	unsigned char c;

	fd = open(pidfile, O_RDONLY);
	if (fd == -1)
		return 0;

	while ((read(fd, &c, 1)) > 0) {
		if (c == '\n')
			break;
		pid = (pid * 10) + (c - '0');
	}
	close(fd);

	return pid;
}


/*
 * Writes out the pidfile
 */
static int write_pid(const char *pidfile, int pid)
{
	FILE *fp;

	if (!(fp = fopen(pidfile, "w")))
		return 0;

	fprintf(fp, "%d\n", pid);
	fclose(fp);

	return pid;
}


/*
 * Checks if a process with the pid found in *pidfile already exists.
 * Returns 1 if it does, and 0 if it doesn't.
 */
static int already_running(const char *pidfile)
{
	int pid = read_pid(pidfile);

	if (!pid)
		return 0;

	if (kill(pid, 0) < 0) {
		if (errno == ESRCH) {
			/* stale pidfile */
			unlink(pidfile);
			return 0;
		}

		fprintf(stderr, "Failed to signal process %d: %s\n",
				pid, strerror(errno));
	}

	return pid;
}

static struct passwd *get_user_entry(const char *user)
{
	struct passwd *pw;

	if (!user || !*user)
		return NULL;

	while ((pw = getpwent())) {
		if (!strcmp(pw->pw_name, user))
			return pw;
	}

	fprintf(stderr, "No such user: %s\n", user);
	exit(EXIT_FAILURE);
}


/*
 * Drop privileges neatly
 *
 * This code was taken from a patch I wrote for the Openwall
 * distro. The patch was/is used in bind, nmap and dhclient
 * shipped with Openwall
 */
static unsigned drop_privs(struct passwd *pw)
{
	/* group first, or we won't be able to swap uid */
	if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0)
		return 0;

	if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0)
		return 0;

	if (getegid() != pw->pw_gid || getgid() != pw->pw_uid)
		return 0;

	if (geteuid() != pw->pw_uid || getuid() != pw->pw_uid)
		return 0;

	return getuid();
}

int kill_daemon(const char *pidfile)
{
	int pid = already_running(pidfile);

	if (pid) {
		printf("Signalling process with pid %d\n", pid);
		kill(pid, SIGTERM);
		sleep(1);
		kill(pid, SIGKILL);
	}
	else
		puts("No daemon running");

	return 0;
}

/*
 * runas is the pseudo-user identity we assume
 * jail is the directory we chdir() to before doing chroot(".")
 * pidfile is written outside the jail.
 * flags is a bitflag option specifier
 */
int daemonize(const char *runas, const char *jail, const char *pidfile, int flags)
{
	struct passwd *pw;
	int pid = already_running(pidfile);

	daemon_pidfile = strdup(pidfile);

	if (pid > 0) {
		fprintf(stderr, "Another instance is already running with pid %d\n", pid);
		exit(EXIT_FAILURE);
	}

	/* don't drop privs or chdir if we're debugging */
	if (flags & DMNZ_NOFORK)
		return write_pid(pidfile, getpid());

	if (jail && chdir(jail) < 0) {
		fprintf(stderr, "Failed to chdir() to '%s': %s\n", jail, strerror(errno));
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork() failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!pid) {
		/* baby daemon goes here */
		if (jail && flags & DMNZ_CHROOT && chroot(".") < 0) {
			fprintf(stderr, "chroot(%s) failed: %s", jail, strerror(errno));
			exit(EXIT_FAILURE);
		}

		pw = get_user_entry(runas);
		if (pw && drop_privs(pw) != pw->pw_uid) {
			fprintf(stderr, "Failed to drop privileges to user %s", pw->pw_name);
			exit(EXIT_FAILURE);
		}
		free(pw);

		if (flags & DMNZ_SIGS) {
			signal(SIGTERM, sighandler);
			signal(SIGINT, sighandler);
		}

		return 0;
	}

	if (write_pid(pidfile, pid) != pid) {
		fprintf(stderr, "Failed to write pidfile '%s': %s\n",
				pidfile, strerror(errno));

		kill(pid, SIGTERM);
		kill(pid, SIGKILL);
		exit(EXIT_FAILURE);
	}

	if (flags & DMNZ_NOFORK)
		return pid;

	_exit(EXIT_SUCCESS);
	return 0;
}
