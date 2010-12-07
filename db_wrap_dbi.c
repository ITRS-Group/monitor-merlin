/**

Concrete db_wrap implementation based off of libdbi.

*/
#include "db_wrap_dbi.h"
#include <string.h> /* strcmp() */

#if 1
#  include <stdio.h>
#  define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__); printf
#  define TODO(X) MARKER("TODO: %s\n",X)
#else
   static void bogo_printf(...) { }
#  define MARKER bogo_printf
#  define TODO(X)
#endif

static int dbiw_connect(db_wrap * self);
static size_t dbiw_sql_quote(db_wrap * self, char const * src, size_t len, char ** dest);
static int dbiw_free_string(db_wrap * self, char * str);
static int dbiw_query_result(db_wrap * self, char const * sql, size_t len, struct db_wrap_result ** tgt);
static int dbiw_error_message(db_wrap * self, char ** dest, size_t * len);
static int dbiw_option_set(db_wrap * self, char const * key, void const * val);
static int dbiw_option_get(db_wrap * self, char const * key, void * val);
static int dbiw_cleanup(db_wrap * self);
static int dbiw_finalize(db_wrap * self);


static int dbiw_res_step(db_wrap_result * self);
static int dbiw_res_get_int32_ndx(db_wrap_result * self, int ndx, int32_t * val);
static int dbiw_res_get_int64_ndx(db_wrap_result * self, int ndx, int64_t * val);
static int dbiw_res_get_double_ndx(db_wrap_result * self, int ndx, double * val);
static int dbiw_res_get_string_ndx(db_wrap_result * self, int ndx, char ** val, size_t * len);
static int dbiw_res_free_string(db_wrap_result * self, char * str);
static int dbiw_res_finalize(db_wrap_result * self);

struct dbiw_db
{
	dbi_conn conn;
};
typedef struct dbiw_db dbiw_db;


static dbiw_db dbiw_db_empty = {
NULL/*connection*/
};

struct dbiw_res_impl
{
	void * placeholder;
};
typedef struct dbiw_res_impl dbiw_res_impl;

static const dbiw_res_impl dbiw_res_impl_empty = {
NULL/*placeholder*/
};


static const db_wrap_result_api dbiw_res_api =
{
	dbiw_res_step,
	dbiw_res_get_int32_ndx,
	dbiw_res_get_int64_ndx,
	dbiw_res_get_double_ndx,
	dbiw_res_get_string_ndx,
	dbiw_res_free_string,
	dbiw_res_finalize
};

static const db_wrap_result dbiw_res_empty =
{
	&dbiw_res_api,
	{/*impl*/
		NULL/*data*/,
		NULL/*dtor*/,
		&dbiw_res_api/*typeID*/
	}
};

static const db_wrap_api db_wrap_api_libdbi = {
dbiw_connect,
dbiw_sql_quote,
dbiw_free_string,
dbiw_query_result,
dbiw_error_message,
dbiw_option_set,
dbiw_option_get,
dbiw_cleanup,
dbiw_finalize
};

static const db_wrap db_wrap_libdbi = {
&db_wrap_api_libdbi,
{/*impl*/
NULL/*data*/,
NULL/*dtor*/,
&db_wrap_api_libdbi/*typeID*/
}
};

#define DB_DECL(ERRVAL) \
	dbiw_db * impl = (self && (self->api==&db_wrap_api_libdbi))   \
		? (dbiw_db *)self->impl.data : NULL;                       \
		if (!impl) return ERRVAL

#define RES_DECL(ERRVAL) \
	dbiw_res_impl * res = (self && (self->api==&dbiw_res_api))   \
		? (dbiw_res_impl *)self->impl.data : NULL; \
		if (!res) return ERRVAL

int dbiw_connect(db_wrap * self)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	int rc = dbi_conn_connect(impl->conn);
	return rc;
}

size_t dbiw_sql_quote(db_wrap * self, char const * src, size_t len, char ** dest)
{
	DB_DECL(0);
	TODO("implement this.");
	return 0;
}
int dbiw_free_string(db_wrap * self, char * str)
{
	DB_DECL(0);
	TODO("implement this.");
	return 0;
}
int dbiw_query_result(db_wrap * self, char const * sql, size_t len, db_wrap_result ** tgt)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_error_message(db_wrap * self, char ** dest, size_t * len)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_option_set(db_wrap * self, char const * key, void const * val)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	int rc = DB_WRAP_E_UNSUPPORTED;
#define TRYSTR(K) if (0==strcmp(K,key)) {                \
		rc = dbi_conn_set_option(impl->conn, key, (char const *)val); }
#define TRYNUM(K) if (0==strcmp(K,key)) {                \
		rc = dbi_conn_set_option_numeric(impl->conn, key, *((int const *)val)); }

	TRYSTR("host")
	else TRYSTR("username")
	else TRYSTR("password")
	else TRYSTR("dbname")
	else TRYNUM("port");
#undef TRYSTR
#undef TRYNUM
	return rc;
}

int dbiw_option_get(db_wrap * self, char const * key, void * val)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_cleanup(db_wrap * self)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	/** Defer all handling to finalize() */
	return 0;
}

int dbiw_finalize(db_wrap * self)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	self->api->cleanup(self);
	if (impl->conn)
	{
		dbi_conn_close(impl->conn);
	}
	*impl = dbiw_db_empty;
	free(impl);
	free(self);
	return 0;
}

int dbiw_res_step(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_get_int32_ndx(db_wrap_result * self, int ndx, int32_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_get_int64_ndx(db_wrap_result * self, int ndx, int64_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_get_double_ndx(db_wrap_result * self, int ndx, double * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_get_string_ndx(db_wrap_result * self, int ndx, char ** val, size_t * len)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_free_string(db_wrap_result * self, char * str)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}

int dbiw_res_finalize(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	TODO("implement this.");
	return -1;
}


int db_wrap_dbi_init(dbi_conn conn, db_wrap_conn_params const * param, db_wrap ** tgt)
{
	if (! conn || !param || !tgt) return DB_WRAP_E_BAD_ARG;
	db_wrap * wr = (db_wrap *)malloc(sizeof(db_wrap));
	if (! wr) return DB_WRAP_E_ALLOC_ERROR;
	dbiw_db * impl = (dbiw_db*)malloc(sizeof(dbiw_db));
	if (! impl)
	{
		free(wr);
		return DB_WRAP_E_ALLOC_ERROR;
	}
	*wr = db_wrap_libdbi;
	wr->impl.data = impl;
#define CLEANUP do{ impl->conn = 0/*caller keeps ownership*/; wr->api->finalize(wr); wr = NULL; } while(0)
#define CHECKRC if (0 != rc) { CLEANUP; return rc; } (void)0
	impl->conn = conn/* do this last, else we'll transfer ownership too early*/;
	int rc = wr->api->option_set(wr, "host", param->host);
	CHECKRC;
	rc = wr->api->option_set(wr, "username", param->user);
	CHECKRC;
	rc = wr->api->option_set(wr, "password", param->password);
	CHECKRC;
	rc = wr->api->option_set(wr, "dbname", param->dbname);
	CHECKRC;
	if (param->port > 0)
	{ /** dbi appears to IGNORE THE PORT i set. If i set an invalid port, it will
		  still connect! Aha... maybe it's defaulting to a socket connection
		  for localhost. */
		rc = wr->api->option_set(wr, "port", &param->port);
		CHECKRC;
	}
	*tgt = wr;
	return 0;
#undef CHECKRC
#undef CLEANUP
}


#undef DB_DECL
#undef RES_DECL
