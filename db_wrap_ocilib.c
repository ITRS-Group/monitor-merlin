/**

Concrete db_wrap implementation based off of ocilib (a.k.a. orclib).

*/

#include <string.h> /* strcmp() */
#include <assert.h>
#include <stdlib.h> /* atexit() */
#include <ocilib.h> /* libdbi */
#include "logging.h" /* lerr() */

#undef TODO
#undef FIXME
#undef MARKER
#if 1 /* for debuggering only */
#  include <stdio.h>
#  define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf
#  define TODO(X) MARKER("TODO: %s\n",X)
#  define FIXME(X) MARKER("FIXME: %s\n",X)
#else
   static void bogo_printf(...) { }
#  define MARKER bogo_printf
#  define TODO(X)
#  define FIXME(X)
#endif

/*
  db_wrap_api member implementations...
*/
static int ociw_connect(db_wrap * self);
static size_t ociw_sql_quote(db_wrap * self, char const * src, size_t len, char ** dest);
static int ociw_free_string(db_wrap * self, char * str);
static int ociw_query_result(db_wrap * self, char const * sql, size_t len, struct db_wrap_result ** tgt);
static int ociw_error_info(db_wrap * self, char const ** dest, size_t * len, int * errCode);
static int ociw_option_set(db_wrap * self, char const * key, void const * val);
static int ociw_option_get(db_wrap * self, char const * key, void * val);
static int ociw_cleanup(db_wrap * self);
static char ociw_is_connected(db_wrap * self);
static int ociw_finalize(db_wrap * self);

/*
  db_wrap_result_api member implementations...
*/
static int ociw_res_step(db_wrap_result * self);
static int ociw_res_get_int32_ndx(db_wrap_result * self, unsigned int ndx, int32_t * val);
static int ociw_res_get_int64_ndx(db_wrap_result * self, unsigned int ndx, int64_t * val);
static int ociw_res_get_double_ndx(db_wrap_result * self, unsigned int ndx, double * val);
static int ociw_res_get_string_ndx(db_wrap_result * self, unsigned int ndx, char const ** val, size_t * len);
static int ociw_res_num_rows(db_wrap_result * self, size_t * num);
static int ociw_res_finalize(db_wrap_result * self);

static const db_wrap_result_api ociw_res_api =
{
	ociw_res_step,
	ociw_res_get_int32_ndx,
	ociw_res_get_int64_ndx,
	ociw_res_get_double_ndx,
	ociw_res_get_string_ndx,
	ociw_res_num_rows,
	ociw_res_finalize
};

struct ociw_result_wrap
{
	OCI_Statement * st;
	OCI_Resultset * result;
};
typedef struct ociw_result_wrap ociw_result_wrap;
#define ociw_result_wrap_empty_m {NULL,NULL}
static const ociw_result_wrap ociw_result_wrap_empty = ociw_result_wrap_empty_m;

static const db_wrap_result ociw_res_empty =
{
	&ociw_res_api,
	{/*impl*/
		NULL/*data*/,
		&ociw_res_api/*typeID*/
	}
};

static const db_wrap_api db_wrap_api_ocilib = {
ociw_connect,
ociw_sql_quote,
ociw_free_string,
ociw_query_result,
ociw_error_info,
ociw_option_set,
ociw_option_get,
ociw_is_connected,
ociw_cleanup,
ociw_finalize
};

struct db_wrap_ocilib_impl
{
	OCI_Connection * conn;
	db_wrap_conn_params params;
};
typedef struct db_wrap_ocilib_impl db_wrap_ocilib_impl;
#define db_wrap_ocilib_impl_empty_m {NULL,db_wrap_conn_params_empty_m}
static const db_wrap_ocilib_impl db_wrap_ocilib_impl_empty = db_wrap_ocilib_impl_empty_m;


static const db_wrap db_wrap_ocilib = {
&db_wrap_api_ocilib,
{/*impl*/
NULL/*data*/,
&db_wrap_api_ocilib/*typeID*/
}
};

static void ociw_atexit()
{
	OCI_Cleanup();
}

static char ociw_oci_init()
{
	static char doneit = 0;
	if (doneit) return 1;
	doneit = 1;
	/*char const * oraHome = getenv("ORA_HOME");
	 */
	char const * libPath =
		//"/home/ora10/OraHome1/lib"
		NULL
		;
	if (! OCI_Initialize(NULL,libPath,OCI_SESSION_DEFAULT | OCI_ENV_CONTEXT))
	{
		lerr("Could not initialize OCI driver!");
		return 0;
	}
	atexit(ociw_atexit);
	return 1;
}


#define INIT_OCI(RC) if (! ociw_oci_init()) return RC

#define IMPL_DECL(ERRVAL)                                         \
	INIT_OCI(ERRVAL); \
	db_wrap_ocilib_impl * dbimpl = (self && (self->api==&db_wrap_api_ocilib))                        \
		? (db_wrap_ocilib_impl *)self->impl.data : NULL; \
	if (! dbimpl) return ERRVAL

#define CONN_DECL(ERRVAL) \
	IMPL_DECL(ERRVAL);                                                  \
	OCI_Connection * conn = dbimpl ? dbimpl->conn : NULL;                \
	if (!conn) return ERRVAL

#define RES_DECL(ERRVAL) \
	ociw_result_wrap * wres = (self && (self->api==&ociw_res_api))   \
		? (ociw_result_wrap*)self->impl.data : NULL;                   \
	OCI_Resultset * ocires = wres->result; \
	if (!ocires) return ERRVAL; \
	INIT_OCI(ERRVAL)

int ociw_connect(db_wrap * self)
{
	MARKER("connect step 1\n");
	IMPL_DECL(DB_WRAP_E_BAD_ARG);
	MARKER("connect step 2\n");
	if (dbimpl->conn)
	{
		MARKER("already connected!\n");
		/** already connected */
		return DB_WRAP_E_BAD_ARG;
	}
	db_wrap_conn_params const * param = &dbimpl->params;
	if (! param->username || !*(param->username))
	{
		MARKER("Bad connection params\n");
		return DB_WRAP_E_BAD_ARG;
	}
	OCI_Connection * conn = OCI_ConnectionCreate(param->dbname,
			                                      param->username,
			                                      param->password,
			                                      OCI_SESSION_DEFAULT);
	if (! conn)
	{
		MARKER("OCI_ConnectionCreate() failed.\n");
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	dbimpl->conn = conn;
	return 0;
}

size_t ociw_sql_quote(db_wrap * self, char const * sql, size_t len, char ** dest)
{
	IMPL_DECL(0);
	if (!sql || !*sql || !len)
	{
		*dest = NULL;
		return 0;
	}
	else
	{
		TODO("implement this");
		return -1;
	}
}

int ociw_free_string(db_wrap * self, char * str)
{
	IMPL_DECL(0);
	free(str);
	return 0;
}
/**
   Allocates a new db_wrap_result object for use with the libdbi
   wrapper. Its api and typeID members are initialized by this call.
   Returns NULL only on alloc error.
*/
static db_wrap_result * ociw_res_alloc()
{
	db_wrap_result * rc = (db_wrap_result*)malloc(sizeof(db_wrap_result));
	if (rc)
	{
		*rc = ociw_res_empty;
	}
	return rc;
}

int ociw_query_result(db_wrap * self, char const * sql, size_t len, db_wrap_result ** tgt)
{
	/*
	  This impl does not use the len param, but it's in the interface because i
	  expect some other wrappers to need it.
	*/
	IMPL_DECL(DB_WRAP_E_BAD_ARG);
	if (! sql || !*sql || !len || !tgt) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_error_info(db_wrap * self, char const ** dest, size_t * len, int * errCode)
{
	if (! self) return DB_WRAP_E_BAD_ARG;
	OCI_Error * err = OCI_GetLastError();
	if (! err)
	{
		if (dest) *dest = NULL;
		if (len) *len = 0;
		if (errCode) *errCode = 0;
		return 0;
	}
	char const * msg = OCI_ErrorGetString(err);
	if (dest) *dest = msg;
	if (len) *len = msg ? strlen(msg) : 0;
	if (errCode) *errCode = OCI_ErrorGetOCICode(err);
	return 0;
}

int ociw_option_set(db_wrap * self, char const * key, void const * val)
{
	return DB_WRAP_E_UNSUPPORTED
		/* OCI lib does not, at first glance, have generic options support.
		   TODO: find the important option funcs and wrap them here. */
		;
}

int ociw_option_get(db_wrap * self, char const * key, void * val)
{
	return DB_WRAP_E_UNSUPPORTED
		/* OCI lib does not, at first glance, have generic options support.
		   TODO: find the important option funcs and wrap them here. */
		;
}

char ociw_is_connected(db_wrap * self)
{
	IMPL_DECL(0);
	return NULL != dbimpl->conn;
}


int ociw_cleanup(db_wrap * self)
{
	IMPL_DECL(DB_WRAP_E_BAD_ARG);
	if (dbimpl->conn)
	{
		OCI_ConnectionFree(dbimpl->conn);
	}
	free(self->impl.data);
	*self = db_wrap_empty;
	return 0;
}

int ociw_finalize(db_wrap * self)
{
	/*MARKER("Freeing db handle @%p\n",(void const *)self);*/
	IMPL_DECL(DB_WRAP_E_BAD_ARG);
	self->api->cleanup(self);
	free(self);
	return 0;
}

int ociw_res_step(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	return OCI_FetchNext(ocires)
		? 0
		: DB_WRAP_E_DONE
		;
}

int ociw_res_get_int32_ndx(db_wrap_result * self, unsigned int ndx, int32_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_res_get_int64_ndx(db_wrap_result * self, unsigned int ndx, int64_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_res_get_double_ndx(db_wrap_result * self, unsigned int ndx, double * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_res_get_string_ndx(db_wrap_result * self, unsigned int ndx, char const ** val, size_t * len)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_res_num_rows(db_wrap_result * self, size_t *num)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! num) return DB_WRAP_E_BAD_ARG;
	TODO("implement this");
	return -1;
}

int ociw_res_finalize(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	/*MARKER("Freeing result handle @%p/@%p\n",(void const *)self, (void const *)res);*/
	if (wres->st)
	{
		OCI_StatementFree(wres->st);
		wres->st = NULL;
		/*
		  NONE of the OCI demo code shows us how to free an OCI_Resultset object (in fact,
		  the demo code mostly doesn't clean up at all), but some demo code implies
		  that cleaning up the statement also cleans the result set.
		 */
		wres->result = NULL;
	}
	else if (wres->result)
	{
		MARKER("POSSIBLE ERROR??? Result object but not Statement?");
	}
	free(self->impl.data);
	*self = ociw_res_empty;
	free(self);
	return 0;
}


int db_wrap_oci_init(db_wrap_conn_params const * param, db_wrap ** tgt)
{
	if (!param || !tgt) return DB_WRAP_E_BAD_ARG;
	INIT_OCI(-1);
	db_wrap * wr = (db_wrap *)malloc(sizeof(db_wrap));
	if (! wr) return DB_WRAP_E_ALLOC_ERROR;
	if (! wr) return DB_WRAP_E_ALLOC_ERROR;
	db_wrap_ocilib_impl * impl = (db_wrap_ocilib_impl *)malloc(sizeof(db_wrap_ocilib_impl));
	if (! impl)
	{
		free(wr);
		return DB_WRAP_E_ALLOC_ERROR;
	}
	*wr = db_wrap_ocilib;
	*impl = db_wrap_ocilib_impl_empty;
	/* Reminder: we cannot set up impl->conn yet. We have to wait
	   until connect() is called.
	*/
	wr->impl.data = impl;
	impl->params = *param;
	*tgt = wr;
	return 0;
}


#undef IMPL_DECL
#undef RES_DECL
#undef INIT_OCI
