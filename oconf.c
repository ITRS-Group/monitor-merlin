#include "module.h"

/*
 * the code in misc.c expects this to be a global variable. Yes, that's
 * weird, and no I'm not gonna do anything about it right now. it is a
 * global variable in Nagios and the module is the designated user of
 * that functionality, so it's not, strictly speaking, necessary.
 */
char *config_file = NULL;

/*
 * converts an arbitrarily long string of data into its
 * hexadecimal representation
 */
char *tohex(const unsigned char *data, int len)
{
	/* number of bufs must be a power of 2 */
	static char bufs[4][41], hex[] = "0123456789abcdef";
	static int bufno;
	char *buf;
	int i;

	buf = bufs[bufno & (ARRAY_SIZE(bufs) - 1)];
	for (i = 0; i < 20 && i < len; i++) {
		unsigned int val = *data++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return bufs[bufno++ & (ARRAY_SIZE(bufs) - 1)];
}

#define CMD_HASH 1
#define CMD_LAST_CHANGE 2
int main(int argc, char **argv)
{
	unsigned char hash[20];
	int i, cmd = 0;
	char *base, *cmd_string = NULL;

	base = strrchr(argv[0], '/');
	if (!base)
		base = argv[0];
	else
		base++;

	if (!prefixcmp(base, "oconf.")) {
		cmd_string = base + strlen("oconf.");
		if (!strcmp(cmd_string, "changed")) {
			cmd = CMD_LAST_CHANGE;
		} else if (!strcmp(cmd_string, "hash")) {
			cmd = CMD_HASH;
		}
	}

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (!prefixcmp(arg, "--nagios-cfg=")) {
			config_file = &arg[strlen("--nagios-cfg=")];
			continue;
		}
		while (*arg == '-')
			arg++;

		if (!prefixcmp(arg, "last") || !prefixcmp(arg, "change")) {
			cmd = CMD_LAST_CHANGE;
			continue;
		}
		if (!strcmp(arg, "sha1") || !strcmp(arg, "hash")) {
			cmd = CMD_HASH;
			continue;
		}
	}
	if (!config_file)
		config_file = "/opt/monitor/etc/nagios.cfg";

	switch (cmd) {
	case CMD_HASH:
		get_config_hash(hash);
		printf("%s\n", tohex(hash, 20));
		break;
	case CMD_LAST_CHANGE:
		printf("%lu\n", get_last_cfg_change());
		break;
	}
	return 0;
}
