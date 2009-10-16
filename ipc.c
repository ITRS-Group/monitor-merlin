#include "shared.h"

#define ipc_read_ok(msec) ipc_poll(POLLIN, msec)
#define ipc_write_ok(msec) ipc_poll(POLLOUT, msec)

static binlog *ipc_binlog;
static char *debug_write, *debug_read;

static int listen_sock = -1; /* for bind() and such */
static int ipc_sock = -1; /* once connected, we operate on this */
static char *ipc_sock_path;
static merlin_event_counter ipc_events;

static time_t last_connect_attempt;

/*
 * these are, if set, run when completing or losing the ipc
 * connection, respectively
 */
static int (*on_connect)(void);
static int (*on_disconnect)(void);

void mrm_ipc_set_connect_handler(int (*handler)(void))
{
	on_connect = handler;
}

void mrm_ipc_set_disconnect_handler(int (*handler)(void))
{
	on_disconnect = handler;
}

void ipc_log_event_count(void)
{
	struct timeval stop;

	gettimeofday(&stop, NULL);
	log_event_count("ipc", &ipc_events, tv_delta(&ipc_events.start, &stop));
}

int ipc_reinit(void)
{
	ipc_deinit();

	return ipc_init();
}

int ipc_accept(void)
{
	struct sockaddr_un saun;
	socklen_t slen = sizeof(struct sockaddr_un);

	if (ipc_sock != -1) {
		lwarn("New connection inbound when one already exists. Dropping old");
		close(ipc_sock);
	}

	ipc_sock = accept(listen_sock, (struct sockaddr *)&saun, &slen);
	if (ipc_sock < 0) {
		lerr("Failed to accept() from listen_sock (%d): %s",
			 listen_sock, strerror(errno));
		return -1;
	}

	/* reset the ipc event counter for each session */
	memset(&ipc_events, 0, sizeof(ipc_events));
	gettimeofday(&ipc_events.start, NULL);

	set_socket_buffers(ipc_sock);

	return ipc_sock;
}

static int ipc_set_sock_path(const char *path)
{
	int result;
	struct stat st;

	/* the sock-path will be set both from module and daemon,
	 * so path must be absolute */
	if (*path != '/')
		return -1;

	if (strlen(path) > UNIX_PATH_MAX)
		return -1;

	xfree(ipc_sock_path);

	ipc_sock_path = strdup(path);
	if (!ipc_sock_path)
		return -1;

	result = stat(path, &st);
	if (result < 0 && errno != ENOENT)
		return -1;

	if (!result && !(st.st_mode & S_IFSOCK))
		return -2;

	return 0;
}

int ipc_grok_var(char *var, char *val)
{
	if (!val)
		return 0;

	if (!strcmp(var, "ipc_socket"))
		return !ipc_set_sock_path(val);

	if (!strcmp(var, "ipc_debug_write")) {
		debug_write = strdup(val);
		return 1;
	}

	if (!strcmp(var, "ipc_debug_read")) {
		debug_read = strdup(val);
		return 1;
	}

	return 0;
}

int ipc_binlog_add(merlin_event *pkt)
{
	if (!ipc_binlog) {
		char *path;

		asprintf(&path, "/opt/monitor/op5/merlin/binlogs/ipc.%s.binlog",
				 is_module ? "module" : "daemon");

		/* 1MB in memory, 100MB on disk */
		ipc_binlog = binlog_create(path, 1 << 20, 100 << 20, BINLOG_UNLINK);
		free(path);

		if (!ipc_binlog)
			return -1;
	}

	binlog_add(ipc_binlog, pkt, packet_size(pkt));
	return 0;
}

int ipc_init(void)
{
	struct sockaddr_un saun;
	struct sockaddr *sa = (struct sockaddr *)&saun;
	socklen_t slen;

	if (!ipc_sock_path) {
		lerr("Attempting to initialize ipc socket, but no socket path has been set\n");
		return -1;
	}

	slen = strlen(ipc_sock_path);

	if (!ipc_sock_path)
		ipc_sock_path = strdup("/opt/monitor/op5/mrd/socket.mrd");

	if (last_connect_attempt + 30 >= time(NULL)) {
		linfo("Initializing IPC socket '%s' for %s", ipc_sock_path,
		      is_module ? "module" : "daemon");
	}

	memset(&saun, 0, sizeof(saun));
	saun.sun_family = AF_UNIX;
	memcpy(saun.sun_path, ipc_sock_path, slen);
	slen += sizeof(struct sockaddr);

	if (listen_sock == -1 || (is_module && ipc_sock == -1)) {
		listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (listen_sock < 0) {
			lerr("Failed to obtain ipc socket: %s", strerror(errno));
			return -1;
		}
	}

	if (!is_module) {
		mode_t old_umask;
		int result;

		if (unlink(ipc_sock_path) && errno != ENOENT)
			return -1;

		slen += offsetof(struct sockaddr_un, sun_path);
		/* Socket is made world writable for now */
		old_umask = umask(0);
		result = bind(listen_sock, sa, slen);
		umask(old_umask);
		if (result < 0) {
			lerr("Failed to bind ipc socket %d to path '%s' with len %d: %s",
				 listen_sock, ipc_sock_path, slen, strerror(errno));
			close(listen_sock);
			listen_sock = -1;
			return -1;
		}

		if (listen(listen_sock, 1) < 0)
			lerr("listen(%d, 1) failed: %s", listen_sock, strerror(errno));

		return 0;
	}

	/* working with the module here */
	if (connect(listen_sock, sa, slen) < 0) {
		if (errno == EISCONN)
			return 0;
		if (last_connect_attempt + 30 <= time(NULL)) {
			lerr("Failed to connect to ipc socket (%d): %s", errno, strerror(errno));
			last_connect_attempt = time(NULL);
		}
		ipc_deinit();
		return -1;
	}
	last_connect_attempt = 0;

	/* reset event counter */
	memset(&ipc_events, 0, sizeof(ipc_events));
	gettimeofday(&ipc_events.start, NULL);

	/* module connected successfully */
	ipc_sock = listen_sock;
	set_socket_buffers(ipc_sock);

	/* let everybody know we're alive and active */
	linfo("Shoutcasting active status through IPC socket");
	ipc_send_ctrl(CTRL_ACTIVE, -1);

	if (on_connect) {
		linfo("Running on_connect hook for module");
		on_connect();
	}

	return 0;
}


void ipc_deinit(void)
{
	/* avoid spurious close() errors while strace/valgrind debugging */
	if (ipc_sock >= 0)
		close(ipc_sock);
	if (listen_sock >= 0)
		close(listen_sock);

	ipc_sock = listen_sock = -1;

	if (!is_module)
		unlink(ipc_sock_path);

	if (on_disconnect)
		on_disconnect();
}


int ipc_is_connected(int msec)
{
	if (is_module) {
		if (ipc_sock < 0)
			return ipc_reinit() == 0;

		return 1;
	}

	if (io_poll(listen_sock, POLLIN, msec) > 0) {
		ipc_sock = ipc_accept();
		if (ipc_sock < 0) {
			lerr("ipc: accept() failed: %s", strerror(errno));
			return 0;
		} else if (on_connect) {
			on_connect();
		}
	}

	return ipc_sock != -1;
}

int ipc_listen_sock_desc(void)
{
	return listen_sock;
}

int ipc_sock_desc(void)
{
	return ipc_sock;
}


static int ipc_poll(int events, int msec)
{
	errno = 0;

	if (ipc_is_connected(0) != 1)
		return 0;

	return io_poll(ipc_sock, events, msec);
}

int ipc_send_ctrl(int control_type, int selection)
{
	if (!ipc_is_connected(0))
		return 0;

	return proto_ctrl(ipc_sock, control_type, selection);
}

int ipc_send_event(merlin_event *pkt)
{
	int result;

	if (!ipc_is_connected(0)) {
		linfo("ipc is not connected");
		if (ipc_binlog_add(pkt) < 0) {
			lwarn("Failed to add packet to binlog. Event dropped");
			ipc_events.dropped++;
		} else {
			ipc_events.logged++;
		}
		return -1;
	}

	if (!ipc_write_ok(100)) {
		linfo("ipc socket isn't ready to accept data: %s", strerror(errno));
		ipc_binlog_add(pkt);
		return -1;
	}

	if (binlog_has_entries(ipc_binlog)) {
		merlin_event *temp_pkt;
		size_t len;

		while (ipc_write_ok(100) && !binlog_read(ipc_binlog, (void **)&temp_pkt, &len)) {
			result = proto_send_event(ipc_sock, temp_pkt);
			if (result < 0 && errno == EPIPE) {
				lerr("Dropped one from ipc backlog");
				ipc_reinit();
			}
		}
	}

	result = proto_send_event(ipc_sock, pkt);
	if (result < 0 && errno == EPIPE) {
		ipc_reinit();
		/* XXX: possible infinite loop */
		ldebug("loop much, do you?");
		return ipc_send_event(pkt);
	}

	return result;
}

int ipc_read_event(merlin_event *pkt)
{
	int poll_result;

	if (ipc_sock < 0) {
		lerr("Asked to read from ipc socket with negative value");
		return -1;
	}

	poll_result = ipc_read_ok(0);
	if (!poll_result)
		return 0;

	if (poll_result < 0) {
		ipc_reinit();
		return -1;
	}

	if (poll_result > 0) {
		int result;
		result = proto_read_event(ipc_sock, pkt);
		if (result < 1) {
			if (result < 0)
				linfo("proto_read_event(%d, ...) failed: %s", ipc_sock, strerror(errno));
			else
				linfo("ipc socket peer disconnected");
			ipc_reinit();
		}
		else {
			ipc_events.read++;
		}
		return result;
	}

	return 0;
}
