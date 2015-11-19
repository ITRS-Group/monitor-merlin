#ifndef TESTS_CUKEMERLIN_CUKESOCKET_H_
#define TESTS_CUKEMERLIN_CUKESOCKET_H_

#include <glib.h>
#include "json.h"

struct CukeSocket_;
typedef struct CukeSocket_ CukeSocket;

typedef gpointer CukeResponseRef;

typedef gpointer (*CukeStepBeginScenario)(void);
typedef void (*CukeStepEndScenario)(gpointer *scenario);
typedef void (*CukeStepHandler)(gpointer *scenario, JsonNode *args, CukeResponseRef respref);

#define STEP_BEGIN(_NAME) \
		static gpointer _NAME(void)
#define STEP_END(_NAME) \
		static void _NAME(gpointer *scenario)
#define STEP_DEF(_NAME) \
		static void _NAME(gpointer *scenario, JsonNode *args, CukeResponseRef respref)

#define STEP_OK \
		cukesock_respond(1, respref, "")
#define STEP_FAIL(_MSG) \
		cukesock_respond(0, respref, (_MSG))

typedef struct CukeStepDefinition_ {
	const gchar *match;
	CukeStepHandler handler;
} CukeStepDefinition;

typedef struct CukeStepEnvironment_ {
	const gchar *tag;
	CukeStepBeginScenario begin_scenario;
	CukeStepEndScenario end_scenario;
	int num_defs; /*< Updated by cukesock_register_stepenv */
	CukeStepDefinition definitions[];
} CukeStepEnvironment;

CukeSocket *cukesock_new(const gchar *bind_addr, const gint bind_port);
void cukesock_destroy(CukeSocket *cs);

void cukesock_respond(int status, CukeResponseRef respref, const gchar *msg);

void cukesock_register_stepenv(CukeSocket *cs, CukeStepEnvironment *stepenv);


#endif /* TESTS_CUKEMERLIN_CUKESOCKET_H_ */
