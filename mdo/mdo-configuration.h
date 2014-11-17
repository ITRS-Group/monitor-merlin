/*
 * mdo-specific configuration
 */

#ifndef INCLUDE_mdo_configuration_h__
#define INCLUDE_mdo_configuration_h__

/**
 * grok second-based intervals, with suffixes.
 * "1h 3m 4s" should return 3600 + 180 + 3 = 3783.
 * "0.5h 0.5m" should return 1800 + 30 = 1830
 * Subsecond precision is not possible (obviously...)
 *
 * Limited to "week" as its highest possible suffix and
 * quite clumsy and forgiving in its parsing. Since we'll
 * most likely be dealing with very short strings I don't
 * care overly about that though.
 */
int grok_seconds(const char *p, long *result);

#endif
