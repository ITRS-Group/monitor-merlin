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

struct ociw_result_impl
{
	OCI_Statement * st;
	OCI_Resultset * result;
};
typedef struct ociw_result_impl ociw_result_impl;
#define ociw_result_impl_empty_m {NULL,NULL}
static const ociw_result_impl ociw_result_impl_empty = ociw_result_impl_empty_m;

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

/**
   An ugly workaround for collection of error information
   reported by ocilib. See comments in ociw_oci_init().
 */
static struct {
	char const * sql;
	char const * errorString;
	int ociCode;
} ociw_error_info_kludge = {
NULL/*sql*/,
NULL/*errorString*/,
0/*ociCode*/
};

static void oci_err_handler(OCI_Error *err)
{
	ociw_error_info_kludge.sql =
		OCI_GetSql(OCI_ErrorGetStatement(err));
	ociw_error_info_kludge.errorString =
		OCI_ErrorGetString(err);
	ociw_error_info_kludge.ociCode =
		OCI_ErrorGetOCICode(err);

	lerr("code  : ORA-%05d\n"
		 "msg   : %s" /*REMEMBER: OCI_ErrorGetString() contains a newline!*/
		 "sql   : %s\n",
		 ociw_error_info_kludge.ociCode,
		 ociw_error_info_kludge.errorString,
		 ociw_error_info_kludge.sql
		);
}

static void ociw_atexit()
{
	OCI_Cleanup();
}

static char ociw_oci_init()
{
	static char doneit = 0;
	if (doneit) return 1;
	doneit = 1;
	char const * libPath =
		//"/home/ora10/OraHome1/lib"
		NULL
		;
	if (! OCI_Initialize(oci_err_handler,
			             libPath,
			             OCI_SESSION_DEFAULT | OCI_ENV_CONTEXT))
		/* reminder: by passing OCI_ENV_CONTEXT we are supposed to get
		   instance-/thread-specific error information via
		   OCI_GetLastError(), but that's not what i'm seeing. i get fed
		   error info via oci_err_handler, but using
		   OCI_GetLastError() still returns no error info.
		   That might be due to this wording from the OCI docs:

		   OCI_GetLastError (void):
		   Retrieve the last error occurred within the last OCILIB call.

		   The problem with that is, we cannot be certain that the
		   error code collection is called immediately after the
		   command which fails (e.g. there might be a
		   cleanup/finalization in between the failure and error
		   collection). OCI is probably re-setting the error state on
		   successfull API calls (e.g. statement finalization, which
		   is one of the corner cases where i say the error state
		   shouldn't be touched).

		   After a short discussion with Andreas, i will implement
		   "the ugly workaround" for the short-term, in which we
		   harvest the information statically from oci_err_handler(),
		   and access that shared/static info from all db_wrap OCI
		   instances.
		*/
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
	if (NULL==dbimpl) return ERRVAL

#define CONN_DECL(ERRVAL) \
	IMPL_DECL(ERRVAL);                                                  \
	OCI_Connection * ociconn = dbimpl ? dbimpl->conn : NULL;                \
	if (NULL==ociconn) return ERRVAL

#define RES_DECL(ERRVAL) \
	ociw_result_impl * wres = (self && (self->api==&ociw_res_api))   \
		? (ociw_result_impl*)self->impl.data : NULL
/* unclear if a null result set is legal for a non-query SQL statement: if (NULL==ocires) return ERRVAL*/

int ociw_connect(db_wrap * self)
{
	IMPL_DECL(DB_WRAP_E_BAD_ARG);
	if (dbimpl->conn)
	{
		/** already connected */
		return DB_WRAP_E_BAD_ARG;
	}
	db_wrap_conn_params const * param = &dbimpl->params;
	if (! param->username || !*(param->username))
	{
		return DB_WRAP_E_BAD_ARG;
	}
	OCI_Connection * conn = OCI_ConnectionCreate(param->dbname,
			                                      param->username,
			                                      param->password,
			                                      OCI_SESSION_DEFAULT);
	if (! conn)
	{
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	OCI_SetAutoCommit(conn, 1);
	dbimpl->conn = conn;
	return 0;
}

size_t ociw_sql_quote(db_wrap * self, char const * sql, size_t len, char ** dest)
{
	IMPL_DECL(0);
	if (! dest) return DB_WRAP_E_BAD_ARG;
	if (!sql || !*sql || !len)
	{
		*dest = NULL;
		return 0;
	}
	else
	{
		/* UNBELIEVABLE:

		   OCI has no routine to escape SQL without immediately sending it to the server!

		   The following is an approximation, where we simply prepend/append quotes and replace
		   all single-quote characters with two single-quote chars.
		*/
		char const q = '\'';
		const size_t sz = (len * 2) /* large enough for a malicious all-quotes string.*/
			+ 2 /* open/closing quotes */
			+ 1 /* NULL pad */
			;
		char * esc = (char *)malloc(sz);
		if (! esc) return DB_WRAP_E_ALLOC_ERROR;
		memset(esc, 0, len);
		char const * p = sql;
		size_t pos = 0;
		*(esc++) = q;
		for(; *p && (pos<len); ++p, ++pos)
		{
			if (q == *p)
			{
			    *(esc++) = q;
			}
			*(esc++) = *p;
		}
		*(esc++) = q;
		*dest = esc;
		return 0;
	}
}

int ociw_free_string(db_wrap * self, char * str)
{
	IMPL_DECL(0);
	free(str);
	return 0;
}

int ociw_query_result(db_wrap * self, char const * sql, size_t len, db_wrap_result ** tgt)
{
	CONN_DECL(DB_WRAP_E_BAD_ARG);
	if (! sql || !*sql || !len || !tgt) return DB_WRAP_E_BAD_ARG;

	OCI_Statement * st = OCI_StatementCreate(ociconn);
	if (! st)
	{
		lerr("Creation of OCI_Statement failed.\n");
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	db_wrap_result * wres = (db_wrap_result*)malloc(sizeof(db_wrap_result));
	if (! wres) return DB_WRAP_E_ALLOC_ERROR;
	*wres = ociw_res_empty;
	ociw_result_impl * impl = (ociw_result_impl*)malloc(sizeof(ociw_result_impl));
	if (! impl)
	{
		OCI_StatementFree(st);
		free(wres);
		return DB_WRAP_E_ALLOC_ERROR;
	}
	*impl = ociw_result_impl_empty;
	impl->st = st;
	wres->impl.data = impl;
#if 1
	if (! OCI_ExecuteStmt(st, sql))
	{
		lerr("Execution of OCI_Statement failed: [%s]\n",OCI_GetSql(st));
		wres->api->finalize(wres);
		/*
		  i don't quite know why, but fetching the OCI error state after this
		  returns a 0 error code and empty error string.
		 */
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
#else
	if (!
		//OCI_ExecuteStmt(st, sql)
		OCI_Prepare(st, sql)
	   )
	{
		lerr("Preparation of OCI_Statement failed: [%s]\n",OCI_GetSql(st));
		wres->api->finalize(wres);
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	if (! OCI_Execute(st))
	{
		lerr("Execution of prepared OCI_Statement failed: [%s]\n",OCI_GetSql(st));
		wres->api->finalize(wres);
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
#endif
	impl->result = OCI_GetResultset(st)
		/* MIGHT be null - the docs are not really clear here what happens on an empty result set. */
		;
	*tgt = wres;
	return 0;
}

int ociw_error_info(db_wrap * self, char const ** dest, size_t * len, int * errCode)
{
	if (! self) return DB_WRAP_E_BAD_ARG;
#if 1
	/* workaround for per-connection error info not being available to
	   us for reasons which haven't been fully determined.

	   This does NOT provide the exact same semantics as the underlying
	   driver, which appears to re-set the error information
	   on each call into the API. Our problem appears to be that we
	   sometimes have several OCI calls before we check the
	   error state, and the error state is gone by the time we
	   can get around to fetching it.
	*/
	if (dest) *dest = ociw_error_info_kludge.errorString;
	if (len) *len = ociw_error_info_kludge.errorString
			      ? strlen(ociw_error_info_kludge.errorString)
			      : 0;
	if (errCode) *errCode = ociw_error_info_kludge.ociCode;
#else
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
#endif
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
	return OCI_FetchNext(wres->result)
		? 0
		: DB_WRAP_E_DONE
		;
}

int ociw_res_get_int32_ndx(db_wrap_result * self, unsigned int ndx, int32_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	*val = OCI_GetInt(wres->result, ndx + 1);
	return 0;
}

int ociw_res_get_int64_ndx(db_wrap_result * self, unsigned int ndx, int64_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	*val = OCI_GetBigInt(wres->result, ndx + 1);
	return 0;
}

int ociw_res_get_double_ndx(db_wrap_result * self, unsigned int ndx, double * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	*val = OCI_GetDouble(wres->result, ndx + 1);
	return 0;
}

int ociw_res_get_string_ndx(db_wrap_result * self, unsigned int ndx, char const ** val, size_t * len)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	char const * str = OCI_GetString(wres->result, ndx + 1);
	*val = str;
	if (len) *len = str ? strlen(str) : 0;
	return 0;
}

int ociw_res_num_rows(db_wrap_result * self, size_t *num)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! num) return DB_WRAP_E_BAD_ARG;
	int rc = OCI_GetRowCount(wres->result);
	if (rc < 0)
	{
		return DB_WRAP_E_UNKNOWN_ERROR;
	}
	*num = (size_t)rc;
	return 0;
}

int ociw_res_finalize(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	/*
	  MARKER("Freeing result handle @%p/%p/@%p\n",(void const *)self, (void const *)wres, (void const *)wres->result);
	*/
	if (wres->st)
	{
		OCI_StatementFree(wres->st);
		wres->st = NULL;
		/*
		  NONE of the OCI demo code shows us how to free an
		  OCI_Resultset object (in fact, the demo code mostly doesn't
		  clean up at all), but the docs state that cleaning up the
		  statement also cleans the result set.
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
