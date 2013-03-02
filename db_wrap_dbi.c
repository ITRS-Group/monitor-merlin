/**

Concrete db_wrap implementation based off of libdbi.

*/

#include <string.h> /* strcmp() */
#include <assert.h>
#include <stdlib.h> /* atexit() */
#include <dbi/dbi.h> /* libdbi */
#include "db_wrap_dbi.h"
#include "mrln_logging.h" /* lerr() */
#undef MARKER
#undef TODO
#undef FIXME
#if 1 /* for debuggering only */
#  include <stdio.h>
#  include <inttypes.h> /* PRIxxx macros, only for debuggering. */
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
static int dbiw_connect(db_wrap * self);
static size_t dbiw_sql_quote(db_wrap * self, char const * src, size_t len, char ** dest);
static int dbiw_free_string(db_wrap * self, char * str);
static int dbiw_query_result(db_wrap * self, char const * sql, size_t len, struct db_wrap_result ** tgt);
static int dbiw_error_info(db_wrap * self, char const ** dest, size_t * len, int * errCode);
static int dbiw_option_set(db_wrap * self, char const * key, void const * val);
static int dbiw_option_get(db_wrap * self, char const * key, void * val);
static int dbiw_cleanup(db_wrap * self);
static char dbiw_is_connected(db_wrap * self);
static int dbiw_finalize(db_wrap * self);
static int dbiw_commit(db_wrap * self);
static int dbiw_set_auto_commit(db_wrap * self, int set);

/*
  db_wrap_result_api member implementations...
*/
static int dbiw_res_step(db_wrap_result * self);
static int dbiw_res_get_int32_ndx(db_wrap_result * self, unsigned int ndx, int32_t * val);
static int dbiw_res_get_int64_ndx(db_wrap_result * self, unsigned int ndx, int64_t * val);
static int dbiw_res_get_double_ndx(db_wrap_result * self, unsigned int ndx, double * val);
static int dbiw_res_get_string_ndx(db_wrap_result * self, unsigned int ndx, char const ** val, size_t * len);
static int dbiw_res_num_rows(db_wrap_result * self, size_t * num);
static int dbiw_res_finalize(db_wrap_result * self);

static const db_wrap_result_api dbiw_res_api =
{
	dbiw_res_step,
	dbiw_res_get_int32_ndx,
	dbiw_res_get_int64_ndx,
	dbiw_res_get_double_ndx,
	dbiw_res_get_string_ndx,
	dbiw_res_num_rows,
	dbiw_res_finalize
};

static const db_wrap_result dbiw_res_empty =
{
	&dbiw_res_api,
	{/*impl*/
		NULL/*data*/,
		&dbiw_res_api/*typeID*/
	}
};

static const db_wrap_api db_wrap_api_libdbi = {
dbiw_connect,
dbiw_sql_quote,
dbiw_free_string,
dbiw_query_result,
dbiw_error_info,
dbiw_option_set,
dbiw_option_get,
dbiw_is_connected,
dbiw_cleanup,
dbiw_finalize,
dbiw_commit,
dbiw_set_auto_commit,
};

static const db_wrap db_wrap_libdbi = {
&db_wrap_api_libdbi,
{/*impl*/
NULL/*data*/,
&db_wrap_api_libdbi/*typeID*/
}
};

/**
   USE_DEPRECATED_DBI_API is a temporary workaround. If it is 1
   then we use the dbi API which is documented on their web site.
   If it is 0 we use the newer, non-deprecated API which appears to
   be undocumented.
*/
#define USE_DEPRECATED_DBI_API 1

#if !USE_DEPRECATED_DBI_API
static dbi_inst DBI_INSTANCE = NULL;
#endif

static void dbiw_atexit(void)
{
#if USE_DEPRECATED_DBI_API
	dbi_shutdown();
#else
	if (DBI_INSTANCE)
	{
		dbi_inst foo = DBI_INSTANCE;
		DBI_INSTANCE = NULL;
		dbi_shutdown_r(foo);
	}
#endif
}
static char dbiw_dbi_init(void)
{
	static char doneit = 0;
	if (doneit) return 1;
	doneit = 1;
	const int rc =
#if USE_DEPRECATED_DBI_API
		dbi_initialize(NULL)
#else
		dbi_initialize_r(NULL,&DBI_INSTANCE)
#endif
		;
	if (0 >= rc)
	{
		lerr("Could not initialize any DBI drivers!");
		return 0;
	}
	atexit(dbiw_atexit);
	return 1;
}


#define INIT_DBI(RC) if (! dbiw_dbi_init()) return RC

#define DB_DECL(ERRVAL)                                         \
	dbi_conn * conn = (self && (self->api==&db_wrap_api_libdbi))   \
		? (dbi_conn *)self->impl.data : NULL;                       \
	if (!conn) return ERRVAL; \
	INIT_DBI(ERRVAL)

#define RES_DECL(ERRVAL) \
	dbi_result dbires = (self && (self->api==&dbiw_res_api))   \
		? (dbi_result)self->impl.data : NULL; \
	/*MARKER("dbi_wrap_result@%p, dbi_result@%p\n",(void const *)self, (void const *)dbires);*/ \
	if (!dbires) return ERRVAL; \
	INIT_DBI(ERRVAL)

int dbiw_connect(db_wrap * self)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	return dbi_conn_connect(conn)
		? DB_WRAP_E_CHECK_DB_ERROR
		: 0;
}

size_t dbiw_sql_quote(db_wrap * self, char const * sql, size_t len, char ** dest)
{
	DB_DECL(0);
	if (!sql || !*sql || !len)
	{
		*dest = NULL;
		return 0;
	}
	else
	{
		return dbi_conn_quote_string_copy(conn, sql, dest);
	}
}

int dbiw_free_string(db_wrap * self, char * str)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	free(str);
	return 0;
}
/**
   Allocates a new db_wrap_result object for use with the libdbi
   wrapper. Its api and typeID members are initialized by this call.
   Returns NULL only on alloc error.
*/
static db_wrap_result * dbiw_res_alloc()
{
	db_wrap_result * rc = (db_wrap_result*)malloc(sizeof(db_wrap_result));
	if (rc)
	{
		*rc = dbiw_res_empty;
	}
	return rc;
}

int dbiw_query_result(db_wrap * self, char const * sql, size_t len, db_wrap_result ** tgt)
{
	/*
	  This impl does not use the len param, but it's in the interface because i
	  expect some other wrappers to need it.
	*/
	DB_DECL(DB_WRAP_E_BAD_ARG);
	if (! sql || !*sql || !len || !tgt) return DB_WRAP_E_BAD_ARG;
	dbi_result dbir = dbi_conn_query(conn, sql);
	if (! dbir) return DB_WRAP_E_CHECK_DB_ERROR;
	db_wrap_result * wres = dbiw_res_alloc();
	if (! wres)
	{
		dbi_result_free(dbir);
		return DB_WRAP_E_ALLOC_ERROR;
	}
	wres->impl.data = dbir;
	/*MARKER("dbi_wrap_result@%p, dbi_result @%p\n", (void const *)wres, (void const *)dbir);*/
	*tgt = wres;
	return 0;
}

int dbiw_error_info(db_wrap * self, char const ** dest, size_t * len, int * errCode)
{
	if (! self) return DB_WRAP_E_BAD_ARG;
	DB_DECL(DB_WRAP_E_BAD_ARG);
	char const * msg = NULL;
	int const code = dbi_conn_error(conn, &msg)
		/* reminder: dbi_conn_error() returns the error code number
		   associated with the fetched string. TODO: consider how we
		   can represent such a dual-use in this API. The native DB
		   APIs i've (namely sqlite3) used don't have such a duality.
		*/
		;
	if (msg && *msg)
	{
		if (dest) *dest = msg;
		if (len) *len = strlen(msg);
		if (errCode) *errCode = code;
	}
	else
	{
		if (dest) *dest = NULL;
		if (len) *len = 0;
		if (errCode) *errCode = 0;
	}
	return 0;
}

int dbiw_option_set(db_wrap * self, char const * key, void const * val)
{
	/**
	   Maintenance note:

	   i HATE hard-coding this property list here, but i have no other way of
	   passing off the options to the proper set_option() variant.
	 */
	DB_DECL(DB_WRAP_E_BAD_ARG);
	int rc = DB_WRAP_E_UNSUPPORTED;
#define TRYSTR(K) if (0==strcmp(K,key)) {                \
		rc = dbi_conn_set_option(conn, key, (char const *)val); }
#define TRYINT(K) if (0==strcmp(K,key)) {                \
		rc = dbi_conn_set_option_numeric(conn, key, *((int const *)val)); }

	TRYSTR("host")
	else TRYSTR("username")
	else TRYSTR("password")
	else TRYSTR("dbname")
	else TRYINT("port")
	else TRYSTR("encoding")
	else TRYSTR("sqlite3_dbdir")
	else TRYINT("sqlite3_timeout")
		/* semicolon gets emacs' indention mode back on the right track */
		;
#undef TRYSTR
#undef TRYINT
	return rc;
}

int dbiw_option_get(db_wrap * self, char const * key, void * val)
{
	/* See maintenance notes in dbiw_option_set(). */
	DB_DECL(DB_WRAP_E_BAD_ARG);
	char const * rcC = NULL;
	int rcI = 0;
#define TRYSTR(K) if (0==strcmp(K,key)) {                \
		rcC = dbi_conn_get_option(conn, key); }
#define TRYNUM(K) if (0==strcmp(K,key)) {                \
		rcI = dbi_conn_get_option_numeric(conn, key); }

	TRYSTR("host")
	else TRYSTR("username")
	else TRYSTR("password")
	else TRYSTR("dbname")
	else TRYNUM("port")
	else TRYSTR("encoding")
	else TRYSTR("sqlite3_dbdir")
	else TRYNUM("sqlite3_timeout")
	else return DB_WRAP_E_UNSUPPORTED;
	if (rcC)
	{
		*((char const **)val) = rcC;
	}
	else
	{
		*((int *)val) = rcI;
	}
	return 0;
}

char dbiw_is_connected(db_wrap * self)
{
	DB_DECL(0);
	return NULL != conn;
}


int dbiw_cleanup(db_wrap * self)
{
	DB_DECL(DB_WRAP_E_BAD_ARG);
	if (conn)
	{
		dbi_conn_close(conn);
	}
	*self = db_wrap_empty;
	return 0;
}

int dbiw_finalize(db_wrap * self)
{
	/*MARKER("Freeing db handle @%p\n",(void const *)self);*/
	DB_DECL(DB_WRAP_E_BAD_ARG);
	self->api->cleanup(self);
	free(self);
	return 0;
}

int dbiw_commit(db_wrap * self)
{
	dbi_result dbir;

	DB_DECL(DB_WRAP_E_BAD_ARG);
	dbir = dbi_conn_query(conn, "COMMIT");
	if (dbir)
		dbi_result_free(dbir);

	return 0;
}

int dbiw_set_auto_commit(db_wrap * self, int set)
{
	dbi_result dbir;
	DB_DECL(DB_WRAP_E_BAD_ARG);
	if (set) {
		dbir = dbi_conn_query(conn, "SET AUTOCOMMIT = 1");
	} else {
		dbir = dbi_conn_query(conn, "SET AUTOCOMMIT = 0");
	}

	if (dbir)
		dbi_result_free(dbir);

	return 0;
}

int dbiw_res_step(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	return dbi_result_next_row(dbires)
		? 0
		: DB_WRAP_E_DONE
		;
}

int dbiw_res_get_int32_ndx(db_wrap_result * self, unsigned int ndx, int32_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	unsigned int const realIdx = ndx+1;
#if 1
	/**
	   See this thread: http://www.mail-archive.com/libdbi-users@lists.sourceforge.net/msg00126.html
	*/
	unsigned int const a = dbi_result_get_field_attrib_idx (dbires, realIdx,
			                                                 0/*DBI_INTEGER_UNSIGNED*/,
			                                                 0xff/*DBI_INTEGER_SIZE8*/)
		/* i can't find one bit of useful docs/examples for this function, so i'm kind of
		   guessing here. */
		;
	//MARKER("Attribute return=0x%x/%u\n",a, a);
	/*assert(0);*/
	/**
	   See this thread: http://www.mail-archive.com/libdbi-users@lists.sourceforge.net/msg00126.html
	*/
	if (DBI_ATTRIBUTE_ERROR == a)
	{
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
#if 0
	else if (0 == a)
	{ /* HORRIBLE KLUDGE for sqlite driver!
	   But after testing, NONE of these return the value i'm expecting! */
		*val = dbi_result_get_short_idx(dbires, realIdx);
		if (!*val) *val = dbi_result_get_ushort_idx( dbires, realIdx );
		if (!*val) *val = dbi_result_get_int_idx( dbires, realIdx );
		if (!*val) *val = dbi_result_get_uint_idx( dbires, realIdx );
		if (!*val) *val = dbi_result_get_longlong_idx( dbires, realIdx );
		if (!*val) *val = dbi_result_get_ulonglong_idx( dbires, realIdx );
		//if (!*val) *val = dbi_result_get_double_idx( dbires, realIdx );
	}
#endif
	else if (DBI_INTEGER_SIZE1 & a)
	{
		/* MARKER("SIZE1\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_uchar_idx(dbires, realIdx)
			: dbi_result_get_char_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE2 & a)
	{
		/* MARKER("SIZE2\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_ushort_idx(dbires, realIdx)
			: dbi_result_get_short_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE4 & a)
	{
		/* MARKER("SIZE4\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_uint_idx(dbires, realIdx)
			: dbi_result_get_int_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE8 & a)
	{
		/* MARKER("SIZE8\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_ulonglong_idx(dbires, realIdx)
			: dbi_result_get_longlong_idx(dbires, realIdx)
			;
	}
	else
	{
		/**
		   libdbi Sqlite driver returns 0 for attributes for
		   the case of SELECT COUNT(*). i have no workaround
		   for this :(.
		*/
		return DB_WRAP_E_UNKNOWN_ERROR;
	}
#else
	*val = dbi_result_get_int_idx(dbires, realIdx);
#endif
	return 0;
}

int dbiw_res_get_int64_ndx(db_wrap_result * self, unsigned int ndx, int64_t * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	unsigned int const realIdx = ndx+1;
	//FIXME("Consolidate the duplicate code in get_int32_ndx() and here.");
#if 1
	/**
	   See this thread: http://www.mail-archive.com/libdbi-users@lists.sourceforge.net/msg00126.html
	*/
	unsigned int const a = dbi_result_get_field_attrib_idx (dbires, realIdx,
			                                                 0,
			                                                 0xff)
		/* i can't find one bit of useful docs/examples for this function, so i'm kind of
		   guessing here. */
		;
	/* MARKER("Attribute return=0x%x/%u\n",a, a); */
	/*assert(0);*/
	if (DBI_ATTRIBUTE_ERROR == a)
	{
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	else if (DBI_INTEGER_SIZE1 & a)
	{
		/* MARKER("SIZE1\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_uchar_idx(dbires, realIdx)
			: dbi_result_get_char_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE2 & a)
	{
		/* MARKER("SIZE2\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_ushort_idx(dbires, realIdx)
			: dbi_result_get_short_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE4 & a)
	{
		/*MARKER("SIZE4\n");*/
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_uint_idx(dbires, realIdx)
			: dbi_result_get_int_idx(dbires, realIdx)
			;
	}
	else if (DBI_INTEGER_SIZE8 & a)
	{
		/* MARKER("SIZE8\n"); */
		*val =
			(a & DBI_DECIMAL_UNSIGNED)
			? dbi_result_get_ulonglong_idx(dbires, realIdx)
			: dbi_result_get_longlong_idx(dbires, realIdx)
			;
	}
	else
	{
		/**
		   libdbi Sqlite driver returns 0 for attributes for
		   the case of SELECT COUNT(*). i have no workaround
		   for this :(.
		*/
		return DB_WRAP_E_UNKNOWN_ERROR;
	}
#else
	//*val = dbi_result_get_int_idx(dbires, realIdx);
	//MARKER("val as int=%"PRIi64"\n",*val);
	//if (!*val)
	*val = dbi_result_get_longlong_idx(dbires, realIdx);
	//MARKER("val as longlong=%"PRIi64"\n",*val);
#endif
	return 0;
}

int dbiw_res_get_double_ndx(db_wrap_result * self, unsigned int ndx, double * val)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	unsigned int const realIdx = ndx+1;
	*val = 0.0;
	unsigned int const a = dbi_result_get_field_attrib_idx (dbires, realIdx, 0, 0xff);
	/* i can't find one bit of useful docs/examples for this function, so i'm kind of
	   guessing here. */
	;
	if (DBI_ATTRIBUTE_ERROR == a)
	{
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	else if (DBI_DECIMAL_SIZE4 & a)
	{
		/* MARKER("SIZE4\n"); */
		*val = dbi_result_get_float_idx(dbires, realIdx);
	}
	else if (DBI_DECIMAL_SIZE8 & a)
	{
		/* MARKER("SIZE8\n"); */
		*val = dbi_result_get_double_idx(dbires, realIdx);
	}
	return 0;
}

int dbiw_res_get_string_ndx(db_wrap_result * self, unsigned int ndx, char const ** val, size_t * len)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! val) return DB_WRAP_E_BAD_ARG;
	char const * str = dbi_result_get_string_idx(dbires, ndx +1);
	if (len)
	{
		*len = (str && *str) ? strlen(str) : 0;
	}
	if (!str || (0==strcmp("ERROR",str)))
	{
		/* libdbi is JUST WRONG here. The convention of using the string
		   "ERROR" to report an error is downright broken, because it
		   prohibits that string as a value in my db.
		*/
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	*val = str;
	return 0;
}

int dbiw_res_num_rows(db_wrap_result * self, size_t *num)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	if (! num) return DB_WRAP_E_BAD_ARG;
	*num = dbi_result_get_numrows(dbires);
	return 0;
}

int dbiw_res_finalize(db_wrap_result * self)
{
	RES_DECL(DB_WRAP_E_BAD_ARG);
	/*MARKER("Freeing result handle @%p/@%p\n",(void const *)self, (void const *)res);*/
	*self = dbiw_res_empty;
	free(self);
	if (dbires)
	{
		dbi_result_free(dbires);

		/*
		  ignore error code in this case, because we really
		  have no recovery strategy if it fails, and we
		  "don't expect" it to ever fail.
		*/
	}
	return 0;
}


int db_wrap_dbi_init(dbi_conn conn, db_wrap_conn_params const * param, db_wrap ** tgt)
{
	if (! conn || !param || !tgt) return DB_WRAP_E_BAD_ARG;
	INIT_DBI(-1);
	db_wrap * wr = (db_wrap *)malloc(sizeof(db_wrap));
	if (! wr) return DB_WRAP_E_ALLOC_ERROR;
	*wr = db_wrap_libdbi;
	wr->impl.data = conn;
	if (param)
	{
#define CLEANUP do{ wr->impl.data = 0/*caller keeps ownership*/; wr->api->finalize(wr); wr = NULL; } while(0)
#define CHECKRC if (0 != rc) { CLEANUP; return rc; } (void)0
		int rc = 0;
#define OPT(K) if (param->K && *param->K) {              \
			rc = wr->api->option_set(wr, #K, param->K);   \
			CHECKRC;                                      \
		}
		OPT(host) OPT(username) OPT(password) OPT(dbname);
#undef OPT
		if (param->port > 0)
		{ /** dbi appears to IGNORE THE PORT i set. If i set an invalid port, it will
			  still connect! Aha... maybe it's defaulting to a socket connection
			  for localhost. */
			rc = wr->api->option_set(wr, "port", &param->port);
			CHECKRC;
		}
	}
	*tgt = wr;
	return 0;
#undef CHECKRC
#undef CLEANUP
}



int db_wrap_dbi_init2(char const * driver, db_wrap_conn_params const * param, db_wrap ** tgt)
{
	INIT_DBI(-1);
	if (! driver || !*driver || !tgt)
	{
		return DB_WRAP_E_BAD_ARG;
	}
	dbi_conn conn =
#if USE_DEPRECATED_DBI_API
		dbi_conn_new(driver)
#else
		dbi_conn_new_r(driver,DBI_INSTANCE)
#endif
		;
	if (! conn)
	{
		return DB_WRAP_E_UNKNOWN_ERROR;
	}
	db_wrap * db = NULL;
	int rc = db_wrap_dbi_init(conn, param, &db);
	if (rc)
	{
		dbi_conn_close(conn);
		return rc;
	}
	assert(db);
	*tgt = db;
	return rc;
}


dbi_result db_wrap_dbi_result(db_wrap_result * self)
{
	RES_DECL(NULL);
	return dbires;
}

dbi_conn db_wrap_dbi_conn(db_wrap * self)
{
	DB_DECL(NULL);
	return conn;
}
#undef DB_DECL
#undef RES_DECL
#undef INIT_DBI
