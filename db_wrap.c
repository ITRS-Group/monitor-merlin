#include "db_wrap.h"
#include <assert.h>
#include <string.h> /* strdup() */
const db_wrap_impl db_wrap_impl_empty = db_wrap_impl_empty_m;
const db_wrap db_wrap_empty = db_wrap_empty_m;
const db_wrap_result db_wrap_result_empty = db_wrap_result_empty_m;
const db_wrap_conn_params db_wrap_conn_params_empty = db_wrap_conn_params_empty_m;
#define TODO(X)

/**
   Experiment: include the driver-specific .c files here so that we
   can avoid adding the extra files to the Makefile. i suspect that
   this will be easier to maintain.
*/
#if DB_WRAP_CONFIG_ENABLE_LIBDBI
#  include "db_wrap_dbi.c"
#endif
#if DB_WRAP_CONFIG_ENABLE_OCILIB
#  include "db_wrap_ocilib.c"
#endif


/**
   Prepares a query which is expected to evaluate to a single value.
   On success 0 is returned and:

   if *res is NULL then the query had no results, and the client might
   decide to use a default value.

   If *res is not NULL then it has been step()'d one time to reach the first
   row, and the client may extract the result from it. The caller must
   dispose of the object using res->api->finalize(res).

   On error non-zero is returned and *res is not modified.
*/
static int db_wrap_query_number_prepare(db_wrap * db, char const * sql, size_t len, db_wrap_result ** tgt)
{
	db_wrap_result * res = NULL;
	int rc;

	if (!(db && sql && *sql && len && tgt) )
	{
		return DB_WRAP_E_BAD_ARG;
	}
	rc = db->api->query_result(db, sql, len, &res);
	if (rc) return rc;
	assert(NULL != res);
	if (! res)
	{
		return DB_WRAP_E_CHECK_DB_ERROR;
	}
	rc = res->api->step(res);
	if (DB_WRAP_E_DONE == rc)
	{
		res->api->finalize(res);
		*tgt = NULL;
		return 0;
	}
	else if (rc)
	{
		res->api->finalize(res);
		return rc;
	}
	else
	{
		*tgt = res;
		return 0;
	}
}
int db_wrap_query_int32(db_wrap * db, char const * sql, size_t len, int32_t * tgt)
{

	db_wrap_result * res = NULL;
	int rc = db_wrap_query_number_prepare(db, sql, len, &res);
	if (rc) return rc;
	else if (! res)
	{
		*tgt = 0;
	}
	else
	{
		rc = res->api->get_int32_ndx(res, 0, tgt);
		res->api->finalize(res);
	}
	return rc;
}
int db_wrap_query_int64(db_wrap * db, char const * sql, size_t len, int64_t * tgt)
{
	db_wrap_result * res = NULL;
	int rc = db_wrap_query_number_prepare(db, sql, len, &res);
	if (rc) return rc;
	else if (! res)
	{
		*tgt = 0;
	}
	else
	{
		rc = res->api->get_int64_ndx(res, 0, tgt);
		res->api->finalize(res);
	}
	return rc;
}
int db_wrap_query_double(db_wrap * db, char const * sql, size_t len, double * tgt)
{
	db_wrap_result * res = NULL;
	int rc = db_wrap_query_number_prepare(db, sql, len, &res);
	if (rc) return rc;
	else if (! res)
	{
		*tgt = 0;
	}
	else
	{
		rc = res->api->get_double_ndx(res, 0, tgt);
		res->api->finalize(res);
	}
	return rc;
}

int db_wrap_query_string(db_wrap * db, char const * sql, size_t len, char const ** tgt, size_t * tgtLen)
{
	db_wrap_result * res = NULL;
	int rc = db_wrap_query_number_prepare(db, sql, len, &res);
	if (rc) return rc;
	else if (! res)
	{
		*tgt = NULL;
		*tgtLen = 0;
		return 0;
	}
	else
	{
		rc = res->api->get_string_ndx(res, 0, tgt, tgtLen);
		res->api->finalize(res);
	}
	return rc;
}


int db_wrap_query_exec(db_wrap * db, char const * sql, size_t len)
{
	db_wrap_result * res = NULL;
	int rc;

	if (!(db && sql && *sql && len) )
	{
		return DB_WRAP_E_BAD_ARG;
	}
	rc = db->api->query_result(db, sql, len, &res);
	if (rc) return rc;
	assert(NULL != res);
	res->api->finalize(res);
	return 0;
}

int db_wrap_result_string_copy_ndx(db_wrap_result * res, unsigned int ndx, char ** sql, size_t *len)
{
	char const * str = NULL;
	int rc;
	if (! res || !sql) return DB_WRAP_E_BAD_ARG;
	rc = res->api->get_string_ndx(res, ndx, &str, len);
	if ((0 == rc) && str) *sql = strdup(str);
	return rc;
}

int db_wrap_driver_init(char const * driver, db_wrap_conn_params const * param, db_wrap ** tgt)
{
	if (! driver || ! param || !tgt) return DB_WRAP_E_BAD_ARG;
#if DB_WRAP_CONFIG_ENABLE_LIBDBI
	char const * prefix = NULL;
	size_t preLen = 0;
	prefix = "dbi:";
	preLen = 4;
	if (0 == strncmp( prefix, driver, preLen) )
	{
		return db_wrap_dbi_init2(driver + preLen, param, tgt);
	}
	else if (0 == strcmp( "mysql", driver) )
	{/* backwards-compatibility hack. */
		return db_wrap_dbi_init2(driver, param, tgt);
	}
#endif
#if DB_WRAP_CONFIG_ENABLE_OCILIB
	if (!strcmp("ocilib", driver) || !strcmp("oracle", driver) || !strcmp("oci", driver))
	{
		/* TODO: ocilib. The main problem here is that i have two dev
		   boxes: one of them has only dbi and one has only ocilib,
		   and i can't get both installed on either machine, primarily
		   because the ocilib-using machine is ancient and has no network
		   access for package updates.
		*/
		return db_wrap_oci_init(param, tgt);
	}
#endif
	return DB_WRAP_E_UNSUPPORTED;
}
