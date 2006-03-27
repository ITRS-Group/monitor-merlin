#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>

static const char *daemon_pidfile;

static void sighandler(int sig)
{
	unlink(daemon_pidfile);

	exit(0);
}

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

static int write_pid(const char *pidfile, int pid)
{
	FILE *fp;

	if (!(fp = fopen(pidfile, "w")))
		return 0;

	fprintf(fp, "%d\n", pid);
	fclose(fp);

	return pid;
}

int already_running(const char *pidfile)
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

struct passwd *get_user_entry(const char *user)
{
	struct passwd *pw;

	if (!user || !*user)
		return NULL;

	while ((pw = getpwent())) {
		if (!strcmp(pw->pw_name, user))
			return pw;
	}

	die("No such user: %s\n", user);
}

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

/* pidfile is written outside the jail.
 * root is the root we take inside the jail (ignore chdir() failure)
 * runas is the pseudo-user identity we assume */
int daemonize(const char *runas, const char *root, const char *pidfile)
{
	struct passwd *pw = get_user_entry(runas);
	int pid = already_running(pidfile);

	daemon_pidfile = strdup(pidfile);

	if (pid > 0) {
		fprintf(stderr, "Another instance is already running with pid %d\n", pid);
		exit(-1);
	}

	/* don't drop privs or chdir if we're debugging */
	if (opts & OPT_DEBUG)
		return write_pid(pidfile, getpid());

	if (root && chdir(root) < 0) {
		fprintf(stderr, "Failed to chdir() to '%s': %s\n", root, strerror(errno));
		return -1;
	}

	if (opts & OPT_DAEMON) {
		pid = fork();
		if (pid < 0)
			die("fork() failed");
	}
	else
		pid = getpid();

	if (pid > 0) {
		if (write_pid(pidfile, pid) != pid) {
			fprintf(stderr, "Failed to write pidfile '%s': %s\n",
					pidfile, strerror(errno));

			kill(pid, SIGTERM);
			kill(pid, SIGKILL);
			exit(-1);
		}

		if (!(opts & OPT_DAEMON))
			return pid;

		_exit(0);
	}

	/* baby daemon goes here */
	if (root && opts & OPT_JAIL && chroot(".") < 0) {
		logerr("chroot(%s) failed: %s", root, strerror(errno));
		exit(1);
	}

	if (pw && drop_privs(pw) != pw->pw_uid) {
		logerr("Failed to drop privileges to user %s", pw->pw_name);
		exit(1);
	}

	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);

	return pid;
}
