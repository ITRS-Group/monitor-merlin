#define _GNU_SOURCE 1 /* for vasprintf */

#include "db_wrap.h"
#include "db_wrap_dbi.h"
#include <assert.h>

#include <stdio.h> /*printf()*/
#include <stdlib.h> /*getenv()*/
#include <string.h> /* strlen() */
#include <inttypes.h> /* PRIuXX macros */
#define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf


static db_wrap_conn_params paramMySql = db_wrap_conn_params_empty_m;
static db_wrap_conn_params paramSqlite = db_wrap_conn_params_empty_m;


void test_libdbi_generic(char const * label, db_wrap * wr)
{
	MARKER("Running generic tests: [%s]\n",label);
	char const * sql = "create "
#if 1
		"temporary "
#endif
		"table t(vint integer)";
	db_wrap_result * res = NULL;
	int rc;

#if 0
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);
	//MARKER("dbi_wrap_result@%p, dbi_result@%p\n",(void const *)res, res->impl.data);
	rc = res->api->finalize(res);
	assert(0 == rc);
	res = NULL;
#else
	rc = db_wrap_query_exec(wr, sql, strlen(sql));
	assert(0 == rc);
#endif

	int i;
	const int count = 10;
	for(i = 1; i <= count; ++i)
	{
		char * q = NULL;
		rc = asprintf(&q,"insert into t (vint) values(%d);",i);
		assert(rc > 0);
		assert(q);
		res = NULL;
		rc = wr->api->query_result(wr, q, strlen(q), &res);
		free(q);
		assert(0 == rc);
		assert(NULL != res);
		rc = res->api->finalize(res);
		assert(0 == rc);
		res = NULL;
	}

	sql =
		"select * from t order by vint desc"
		;
	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);
	assert(res->impl.data == db_wrap_dbi_result(res));

	/* ensure that stepping acts as expected. */
	int gotCount = 0;
	while(0 == (rc = res->api->step(res)))
	{
		++gotCount;
	}
	assert(gotCount == count);
	assert(DB_WRAP_E_DONE == rc);
	res->api->finalize(res);

	// FIXME: add reset() to the result API.

	/**
	   Now try fetching some values...
	*/
	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);

#if 1
	//#error "Something is still wrong here: get_int_by_index is always returning 0."
	int32_t intGet = -1;
	const int32_t intExpect = count;
	dbi_result dbires = NULL;
#if 0
	dbires = db_wrap_dbi_result(res);
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#elseif 0
	dbires = (dbi_result)res->impl.data;
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#else
	dbires = NULL;
	rc = res->api->step(res);
	assert(0 == rc);
	rc = res->api->get_int32_ndx(res, 0, &intGet);
	assert(0 == rc);
#endif
	//MARKER("got int=%d\n",intGet);
	assert(intGet == intExpect);
#endif
	rc = res->api->finalize(res);
	assert(0 == rc);

	intGet = -1;
	rc = db_wrap_query_int32(wr, sql, strlen(sql), &intGet);
	assert(0 == rc);
	assert(intGet == intExpect);

	int64_t int64Get = -1;
	rc = db_wrap_query_int64(wr, sql, strlen(sql), &int64Get);
	assert(0 == rc);
	assert(intGet == (int)int64Get);
#if 0
	// not yet working. don't yet know why
	double doubleGet = -1.0;
	rc = db_wrap_query_double(wr, sql, strlen(sql), &doubleGet);
	MARKER("doubleGet=%lf\n",doubleGet);
	assert(0 == rc);
	assert(intGet == (int)doubleGet);
#endif

}

void test_mysql_1()
{
	db_wrap * wr = NULL;
	dbi_conn conn = dbi_conn_new("mysql");
	assert(conn);
	int rc = db_wrap_dbi_init(conn, &paramMySql, &wr);
	assert(0 == rc);
	assert(wr);
	rc = wr->api->connect(wr);
	assert(0 == rc);

	char * sqlCP = NULL;
	char const * sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	/* ACHTUNG: what libdbi does here with the escaping is NOT SQL STANDARD. */
	assert(0 == strcmp("'hi, \\'world\\''", sqlCP) );
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	test_libdbi_generic("mysql",wr);

	rc = wr->api->finalize(wr);
	assert(0 == rc);
}

void test_sqlite_1()
{
	db_wrap * wr = NULL;
	dbi_conn conn = dbi_conn_new("sqlite3");
	assert(conn);
	int rc = db_wrap_dbi_init(conn, &paramSqlite, &wr);
	assert(0 == rc);
	assert(wr);
	char const * dbdir = getenv("PWD");
	rc = wr->api->option_set(wr, "sqlite3_dbdir", dbdir);
	assert(0 == rc);
	rc = wr->api->connect(wr);
	assert(0 == rc);
	char * errmsg = NULL;
	rc = wr->api->error_message(wr, &errmsg, NULL);
	assert(0 == rc);
	assert(NULL == errmsg);

	char * sqlCP = NULL;
	char const * sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	assert(0 == strcmp("'hi, ''world'''", sqlCP) );
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	sql = NULL;
	rc = wr->api->option_get(wr, "sqlite3_dbdir", &sql);
	assert(0 == rc);
	assert(0 == strcmp( sql, dbdir) );

	test_libdbi_generic("sqlite3",wr);


	rc = wr->api->finalize(wr);
	assert(0 == rc);
}

int main(int argc, char const ** argv)
{
	{
		paramMySql.host = "localhost";
		paramMySql.port = 3306;
		paramMySql.username = "merlin";
		paramMySql.password = "merlin";
		paramMySql.dbname = "merlin";
	}
	{
		paramSqlite.dbname = "merlin.sqlite";
	}
	dbi_initialize(NULL);
	test_mysql_1();
	test_sqlite_1();
	dbi_shutdown();
	MARKER("If you got this far, it worked.\n");
	return 0;
}
