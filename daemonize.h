#ifndef INCLUDE_daemonize_h__
#define INCLUDE_daemonize_h__

#define DMNZ_NOFORK  1
#define DMNZ_CHROOT (1 << 1)
#define DMNZ_SIGS   (1 << 2)

extern int kill_daemon(const char *pidfile);
extern int daemonize(const char *runas, const char *jail,
                     const char *pidfile, int flags);

#endif
