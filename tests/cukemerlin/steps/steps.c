#include <base/cukesocket.h>
#include "steps.h"

#include "steps_test.h"
#include "steps_merlin.h"
#include "steps_daemons.h"
#include "steps_config.h"
#include "steps_queryhandler.h"

void steps_load(CukeSocket *cs) {
	cukesock_register_stepenv(cs, &steps_test);
	cukesock_register_stepenv(cs, &steps_merlin);
	cukesock_register_stepenv(cs, &steps_daemons);
	cukesock_register_stepenv(cs, &steps_config);
	cukesock_register_stepenv(cs, &steps_queryhandler);
}
