#include "shared.h"

#define ipc_read_ok(msec) ipc_poll(POLLIN, msec)
#define ipc_write_ok(msec) ipc_poll(POLLOUT, msec)

static int listen_sock = -1; /* for bind() and such */
static char *ipc_sock_path;
static char *ipc_binlog_path, *ipc_binlog_dir = "/opt/monitor/op5/merlin/binlogs";
static int sync_lost;
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

	/* reset sync state */
	sync_lost = 0;

	/* reset the ipc event counter for each session */
	memset(&ipc.events, 0, sizeof(ipc.events));
	gettimeofday(&ipc.events.start, NULL);

	set_socket_buffers(ipc.sock);

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

static int ipc_binlog_add(merlin_event *pkt)
{
	if (!ipc.binlog) {
		char *path;

		if (ipc_binlog_path)
			path = ipc_binlog_path;
		else 
			asprintf(&path, "%s/ipc.%s.binlog", ipc_binlog_dir,
					 is_module ? "module" : "daemon");

		linfo("Creating binary ipc backlog. On-disk location: %s", path);
		/* 10MB in memory, 100MB on disk */
		ipc.binlog = binlog_create(path, 10 << 20, 100 << 20, BINLOG_UNLINK);
		if (path != ipc_binlog_path)
			free(path);

		if (!ipc.binlog) {
			lerr("Failed to create binary ipc backlog: %s", strerror(errno));
			return -1;
		}
	}

	if (binlog_add(ipc.binlog, pkt, packet_size(pkt)) < 0) {
		if (sync_lost) {
			ipc.events.dropped++;
			return -1;
		}

		sync_lost = 1;

		/*
		 * first message we couldn't deliver, so wipe the binary
		 * log and take whatever action is appropriate
		 */
		binlog_wipe(ipc.binlog, BINLOG_UNLINK);

		/* update counters now that we'll be dropping the binlog */
		ipc.events.dropped += ipc.events.logged;
		ipc.events.logged = 0;

		lerr("Failed to add %u bytes to binlog with path '%s': %s",
			 packet_size(pkt), binlog_path(ipc.binlog), strerror(errno));
		ipc_sync_lost();
		return -1;
	}
	ipc.events.logged++;

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

	/* let everybody know we're alive and active */
	linfo("Shoutcasting active status through IPC socket %s", ipc_sock_path);
	ipc_send_ctrl(CTRL_ACTIVE, -1);

	if (on_connect) {
		linfo("Running on_connect hook");
		sync_lost = 0;
		on_connect();
	}

	return 0;
}


void ipc_deinit(void)
{
	/* avoid spurious close() errors while strace/valgrind debugging */
	if (ipc.sock >= 0)
		close(ipc.sock);
	if (listen_sock >= 0)
		close(listen_sock);

	ipc.sock = listen_sock = -1;

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

		return 1;
	}

	if (io_poll(listen_sock, POLLIN, msec) > 0) {
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


static int ipc_poll(int events, int msec)
{
	errno = 0;

	if (ipc_is_connected(0) != 1)
		return 0;

	return io_poll(ipc.sock, events, msec);
}

int ipc_send_ctrl(int control_type, int selection)
{
	merlin_event pkt;
	int result;

	memset(&pkt.hdr, 0, HDR_SIZE);
	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.code = control_type;
	pkt.hdr.selection = selection;

	result = node_send_event(&ipc, &pkt);
	if (result < 0)
		return ipc_binlog_add(&pkt);

	return result;
}

int ipc_send_event(merlin_event *pkt)
{
	int result;

	ipc_log_event_count();

	/*
	 * we might be ok with not being able to send if we can
	 * add stuff to the binlog properly, since that one gets
	 * emptied by us too
	 */
	if (!ipc_is_connected(0) || !ipc_write_ok(100)) {
		return ipc_binlog_add(pkt);
	}

	/* if the binlog has entries, we must send those first */
	if (binlog_has_entries(ipc.binlog)) {
		merlin_event *temp_pkt;
		uint len;

		/*
		 * we use a slightly higher timeout here, as we'll be
		 * spraying the daemon pretty hard
		 */
		linfo("binary backlog has entries. Emptying those first");
		while (ipc_write_ok(500) && !binlog_read(ipc.binlog, (void **)&temp_pkt, &len)) {
			result = node_send_event(&ipc, temp_pkt);

			/*
			 * the binlog duplicates the memory, so we must
			 * free it here or it will be leaked
			 */
			free(temp_pkt);

			/*
			 * an error when sending the backlogged entries
			 * means we've lost sync
			 */
			if (result < 0 && errno == EPIPE) {
				binlog_wipe(ipc.binlog, BINLOG_UNLINK);
				ipc_sync_lost();
				ipc_reinit();
				return -1;
			}
		}
	}

	result = node_send_event(&ipc, pkt);
	if (result < 0 && errno == EPIPE) {
		ipc_reinit();
		/* XXX: possible infinite loop */
		ldebug("loop much, do you?");
		return ipc_send_event(pkt);
	}

	ipc.events.sent++;

	return result;
}

int ipc_read_event(merlin_event *pkt, int msec)
{
	int poll_result;

	if (ipc.sock < 0) {
		lerr("Asked to read from ipc socket with negative value");
		return -1;
	}

	poll_result = ipc_read_ok(msec);
	if (!poll_result)
		return 0;

	if (poll_result < 0) {
		ipc_reinit();
		return -1;
	}

	if (poll_result > 0) {
		int result;
		result = node_read_event(&ipc, pkt);
		if (result < 1) {
			if (result < 0)
				linfo("proto_read_event(%d, ...) failed: %s", ipc.sock, strerror(errno));
			else
				linfo("ipc socket peer disconnected");
			ipc_reinit();
		}
		else {
			ipc.events.read++;
		}
		return result;
	}

	return 0;
}
