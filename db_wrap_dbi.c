/**

Concrete db_wrap implementation based off of libdbi.

*/
#include "db_wrap_dbi.h"
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
#if 0
	char * host;
	char *name;
	char *user;
	char *pass;
	char *type;
	char *encoding;
	unsigned int port;
#endif
	dbi_conn conn;
#if 0
	dbi_result result;
	dbi_driver driver;
#endif
};
typedef struct dbiw_db dbiw_db;
static dbiw_db dbiw_db_empty = {
#if 0
NULL/*host*/,
NULL/*name*/,
NULL/*user*/,
NULL/*pass*/,
NULL/*type*/,
NULL/*encoding*/,
0/*port*/,
#endif
NULL/*connection*/
#if 0
NULL/*result*/,
NULL/*driver*/
#endif
};

struct dbiw_res_impl
{
	void * placeholder;
};
typedef struct dbiw_res_impl dbiw_res_impl;

/*static*/ dbiw_res_impl dbiw_res_impl_empty = {
NULL/*placeholder*/
};


static const db_wrap_result dbiw_res_empty =
{
	dbiw_res_step,
	dbiw_res_get_int32_ndx,
	dbiw_res_get_int64_ndx,
	dbiw_res_get_double_ndx,
	dbiw_res_get_string_ndx,
	dbiw_res_free_string,
	dbiw_res_finalize,
	{/*impl*/
		NULL/*data*/,
		NULL/*dtor*/,
		&dbiw_res_empty/*typeID*/
	}
};


static const db_wrap db_wrap_libdbi = {
dbiw_connect,
dbiw_sql_quote,
dbiw_free_string,
dbiw_query_result,
dbiw_error_message,
dbiw_option_set,
dbiw_option_get,
dbiw_cleanup,
dbiw_finalize,
{/*impl*/
NULL/*data*/,
NULL/*dtor*/,
&dbiw_db_empty/*typeID*/
}
};

#define DB_DECL(ERRVAL) \
	dbiw_db * impl = (self && (self->impl.typeID==&dbiw_db_empty))   \
		? (dbiw_db *)self->impl.data : NULL;                       \
		if (!impl) return ERRVAL

#define RES_DECL(ERRVAL) \
	dbiw_res_impl * res = (self && (self->impl.typeID==&dbiw_res_empty))   \
		? (dbiw_res_impl *)self->impl.data : NULL; \
		if (!res) return ERRVAL

int dbiw_connect(db_wrap * self)
{
	TODO("implement this.");
	return -1;

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
	TODO("implement this.");
	return -1;
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
	self->cleanup(self);
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
#define CLEANUP do{ wr->finalize(wr); wr = NULL; } while(0)
#define CHECKRC if (0 != rc) { CLEANUP; return rc; } (void)0
	int rc = dbi_conn_set_option(conn, "host", param->host);
	CHECKRC;
	rc = dbi_conn_set_option(conn, "username", param->user);
	CHECKRC;
	rc = dbi_conn_set_option(conn, "password", param->password);
	CHECKRC;
	rc = dbi_conn_set_option(conn, "dbname", param->dbname);
	CHECKRC;
	if (param->port > 0)
	{
		rc = dbi_conn_set_option_numeric(conn, "port", param->port);
		CHECKRC;
	}
	impl->conn = conn/* do this last, else we'll transfer ownership too early*/;
	*tgt = wr;
	return 0;
#undef CHECKRC
#undef CLEANUP
}


#undef DB_DECL
#undef RES_DECL
