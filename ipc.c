#include "shared.h"

static int listen_sock = -1; /* for bind() and such */
static char *ipc_sock_path;
static char *ipc_binlog_path, *ipc_binlog_dir = "/opt/monitor/op5/merlin/binlogs";
static time_t last_connect_attempt;
static merlin_node ipc = { "ipc", -1, -1, 0, 0 }; /* the ipc node */

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
	node_log_event_count(&ipc, 0);
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

	if (ipc.sock != -1) {
		lwarn("New connection inbound when one already exists. Dropping old");
		close(ipc.sock);
	}

	ipc.sock = accept(listen_sock, (struct sockaddr *)&saun, &slen);
	if (ipc.sock < 0) {
		lerr("Failed to accept() from listen_sock (%d): %s",
			 listen_sock, strerror(errno));
		return -1;
	}

	/* reset the ipc event counter for each session */
	memset(&ipc.events, 0, sizeof(ipc.events));
	gettimeofday(&ipc.events.start, NULL);

	set_socket_buffers(ipc.sock);
	ipc.status = STATE_CONNECTED;

	/* run daemon's on-connect handlers */
	if (on_connect)
		on_connect();

	return ipc.sock;
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

	safe_free(ipc_sock_path);

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

static int ipc_set_binlog_path(const char *path)
{
	int result;

	/* the binlog-path will be set both from module and daemon,
	   so path must be absolute */
	if (*path != '/') {
		lerr("ipc_binlog path must be absolute");
		return -1;
	}

	if (strlen(path) > UNIX_PATH_MAX)
		return -1;

	if (path[strlen(path)-1] == '/') {
		lerr("ipc_binlog must not end in trailing slash");
		return -1;
	}

	safe_free(ipc_binlog_path);

	ipc_binlog_path = strdup(path);
	if (!ipc_binlog_path) {
		lerr("ipc_binlog_set_path: could not strdup path, out of memory?");
		return -1;
	}

	/* Test to make sure that the path will be usable when we need it. */
	result = open(path, O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
	if (result < 0) {
		lerr("Error opening '%s' for writing, failed with error: %s",
		     path, strerror(errno));
		return -1;
	}
	close(result);
	unlink(path);

	return 0;
}

int ipc_grok_var(char *var, char *val)
{
	if (!val)
		return 0;

	if (!strcmp(var, "ipc_socket"))
		return !ipc_set_sock_path(val);

	if (!strcmp(var, "ipc_binlog"))
		return !ipc_set_binlog_path(val);

	if (!strcmp(var, "ipc_binlog_dir") || !strcmp(var, "ipc_backlog_dir")) {
		ipc_binlog_dir = strdup(val);
		return 1;
	}

	return 0;
}

int ipc_init(void)
{
	struct sockaddr_un saun;
	struct sockaddr *sa = (struct sockaddr *)&saun;
	socklen_t slen;
	int quiet = 0;

	/* don't spam the logs */
	if (last_connect_attempt + 30 >= time(NULL)) {
		quiet = 1;
	}
	last_connect_attempt = time(NULL);

	if (!ipc_sock_path) {
		lerr("Attempting to initialize ipc socket, but no socket path has been set\n");
		return -1;
	}

	slen = strlen(ipc_sock_path);

	if (!ipc_sock_path)
		ipc_sock_path = strdup("/opt/monitor/op5/mrd/socket.mrd");

	memset(&ipc.events, 0, sizeof(ipc.events));
	gettimeofday(&ipc.events.start, NULL);
	if (!quiet) {
		linfo("Initializing IPC socket '%s' for %s", ipc_sock_path,
		      is_module ? "module" : "daemon");
	}

	memset(&saun, 0, sizeof(saun));
	saun.sun_family = AF_UNIX;
	memcpy(saun.sun_path, ipc_sock_path, slen);
	slen += sizeof(struct sockaddr);

	if (listen_sock == -1 || (is_module && ipc.sock == -1)) {
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
			if (!quiet) {
				lerr("Failed to bind ipc socket %d to path '%s' with len %d: %s",
					 listen_sock, ipc_sock_path, slen, strerror(errno));
			}
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
		if (!quiet) {
			lerr("Failed to connect to ipc socket '%s': %s", ipc_sock_path, strerror(errno));
		}
		ipc_deinit();
		return -1;
	}
	last_connect_attempt = 0;

	/* reset event counter */
	memset(&ipc.events, 0, sizeof(ipc.events));
	gettimeofday(&ipc.events.start, NULL);

	/* module connected successfully */
	ipc.sock = listen_sock;
	set_socket_buffers(ipc.sock);
	ipc.status = STATE_CONNECTED;

	/* let everybody know we're alive and active */
	linfo("Shoutcasting active status through IPC socket %s", ipc_sock_path);
	ipc_send_ctrl(CTRL_ACTIVE, -1);

	if (on_connect) {
		linfo("Running on_connect hook");
		on_connect();
	}

	return 0;
}


void ipc_deinit(void)
{
	node_disconnect(&ipc);

	/* avoid spurious valgrind/strace warnings */
	if (listen_sock >= 0)
		close(listen_sock);

	listen_sock = -1;

	if (!is_module)
		unlink(ipc_sock_path);

	if (on_disconnect)
		on_disconnect();
}


int ipc_is_connected(int msec)
{
	if (is_module) {
		if (ipc.sock < 0)
			return ipc_reinit() == 0;

		ipc.status = STATE_CONNECTED;
		return 1;
	}

	if (io_read_ok(listen_sock, msec) > 0) {
		ipc.sock = ipc_accept();
		if (ipc.sock < 0) {
			lerr("ipc: accept() failed: %s", strerror(errno));
			return 0;
		} else if (on_connect) {
			on_connect();
		}
	}

	return ipc.sock != -1;
}

int ipc_listen_sock_desc(void)
{
	return listen_sock;
}

int ipc_sock_desc(void)
{
	return ipc.sock;
}

int ipc_send_ctrl(int control_type, int selection)
{
	return node_send_ctrl(&ipc, control_type, selection, 50);
}

int ipc_send_event(merlin_event *pkt)
{
	ipc_is_connected(0);
	if (node_send_event(&ipc, pkt, 100) < 0) {
		ipc_reinit();
		return -1;
	}

	return 0;
}

int ipc_read_event(merlin_event *pkt, int msec)
{
	if (!ipc_is_connected(0) || io_read_ok(ipc.sock, msec) <= 0)
		return 0;

	return node_read_event(&ipc, pkt, msec);
}
