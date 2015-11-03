#include <base/cukesocket.h>
#include "steps.h"

#include "steps_test.h"

void steps_load(CukeSocket *cs) {
	cukesock_register_stepenv(cs, &steps_test);
}
