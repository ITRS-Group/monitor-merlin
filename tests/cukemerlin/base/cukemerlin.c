#include <glib.h>
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <naemon/naemon.h>

#include "cukesocket.h"
#include "json.h"

#include <steps/steps.h>

static gchar *opt_bind_address = "0.0.0.0";
static gint opt_bind_port = 31221;

static GMainLoop *mainloop = NULL;

static void stop_mainloop(int signal);

static GOptionEntry opt_entries[] = {
		{ "bind-address", 'a', 0, G_OPTION_ARG_STRING, &opt_bind_address,
				"Bind to this address", "addr" },
		{ "bind-port", 'p', 0, G_OPTION_ARG_INT, &opt_bind_port,
				"Listen to this port", "port" }, { NULL } };

static int drop_privileges(char *user, char *group)
{
	uid_t uid = -1;
	gid_t gid = -1;
	struct group *grp = NULL;
	struct passwd *pw = NULL;
	int result = OK;

	/*
	 * only drop privileges if we're running as root, so we don't interfere
	 * with being debugged while running as some random user
	 */
	if (getuid() != 0)
		return OK;

	/* set effective group ID */
	if (group != NULL) {

		/* see if this is a group name */
		if (strspn(group, "0123456789") < strlen(group)) {
			grp = getgrnam(group);
			if (grp != NULL)
				gid = grp->gr_gid;
			else
				g_print("Warning: Could not get group entry for '%s'\n", group);
		}

		/* else we were passed the GID */
		else
			gid = (gid_t)atoi(group);
	}

	/* set effective user ID */
	if (user != NULL) {

		/* see if this is a user name */
		if (strspn(user, "0123456789") < strlen(user)) {
			pw = getpwnam(user);
			if (pw != NULL)
				uid = pw->pw_uid;
			else
				g_print("Warning: Could not get passwd entry for '%s'\n", user);
		}

		/* else we were passed the UID */
		else
			uid = (uid_t)atoi(user);
	}

	/* set effective group ID if other than current EGID */
	if (gid != getegid()) {
		if (setgid(gid) == -1) {
			g_print("Warning: Could not set effective GID=%d\n", (int)gid);
			result = ERROR;
		}
	}
#ifdef HAVE_INITGROUPS

	if (uid != geteuid()) {

		/* initialize supplementary groups */
		if (initgroups(user, gid) == -1) {
			if (errno == EPERM)
				g_print("Warning: Unable to change supplementary groups using"
						"initgroups()-- I hope you know what you're doing\n");
			else {
				g_print("Warning: Possibly root user failed dropping privileges"
						"with initgroups()\n");
				return ERROR;
			}
		}
	}
#endif
	if (setuid(uid) == -1) {
		g_print("Warning: Could not set effective UID=%d\n", (int)uid);
		result = ERROR;
	}

	return result;
}

int main(int argc, char *argv[]) {
	GOptionContext *optctx;
	GError *error = NULL;
	CukeSocket *cs = NULL;

	g_type_init();

	optctx = g_option_context_new("- Merlin protocol cucumber test daemon");
	g_option_context_add_main_entries(optctx, opt_entries, NULL);
	if (!g_option_context_parse(optctx, &argc, &argv, &error)) {
		g_print("%s\n", error->message);
		g_error_free(error);
		exit(1);
	}

	mainloop = g_main_loop_new(NULL, TRUE);
	/* non glib-unix version of stopping signals */
	signal(SIGHUP, stop_mainloop);
	signal(SIGINT, stop_mainloop);
	signal(SIGTERM, stop_mainloop);

	cs = cukesock_new(opt_bind_address, opt_bind_port);
	g_return_val_if_fail(cs != NULL, 1);

	steps_load(cs);

	/*
	 * We are not allowed to run naemon as root, so before we run any tests
	 * involving naemon, we drop root privileges if we have it and run as
	 * naemon user instead.
	 */
	drop_privileges(NAEMON_USER, NAEMON_GROUP);

	g_message("Main Loop: Enter");
	g_main_loop_run(mainloop);
	g_message("Main Loop: Exit");

	cukesock_destroy(cs);
	g_main_loop_unref(mainloop);

	g_option_context_free(optctx);

	return 0;
}

static void stop_mainloop(int signal) {
	g_main_loop_quit(mainloop);
}
