#ifndef TESTS_CUKEMERLIN_STEPS_STEPS_H_
#define TESTS_CUKEMERLIN_STEPS_STEPS_H_

#include <base/cukesocket.h>

/**
 * Load all step definitions available into the given CukeSocket. This just
 * isolates the implementation of step definitions from the cukemerlin basedir
 */
void steps_load(CukeSocket *cs);

#endif /* TESTS_CUKEMERLIN_STEPS_STEPS_H_ */
