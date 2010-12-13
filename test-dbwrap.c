#define _GNU_SOURCE 1 /* for vasprintf */

#include "db_wrap.h"
#include "db_wrap_dbi.h"
#include <assert.h>

#include <stdio.h> /*printf()*/
#include <stdlib.h> /*getenv(), atexit()*/
#include <string.h> /* strlen() */
#include <inttypes.h> /* PRIuXX macros */
#include <stdbool.h>
#define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf
#define FIXME(X) MARKER("FIXME: " X)

static db_wrap_conn_params paramMySql = db_wrap_conn_params_empty_m;
static db_wrap_conn_params paramSqlite = db_wrap_conn_params_empty_m;

static struct {
	bool useTempTables;
	bool testMySQL;
	bool testSQLite3;
} ThisApp = {
true/*useTempTables*/,
true/*testMySQL*/,
true/*testSQLite3*/
};

void test_libdbi_generic(char const * label, db_wrap * wr)
{
	MARKER("Running generic tests: [%s]\n",label);
#define TABLE_DEF \
	"table t(vint integer, vdbl float(12,4), vstr varchar(32))"
	char const * sql = ThisApp.useTempTables
		? ("create temporary " TABLE_DEF)
		: ("create " TABLE_DEF)
		;
#undef TABLE_DEF
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
	char const * strVal = "hi, world";
	for(i = 1; i <= count; ++i)
	{
		char * q = NULL;
		rc = asprintf(&q,"insert into t (vint, vdbl, vstr) values(%d,%2.1lf,'%s');",
			          i,(i*1.1),strVal);
		assert(rc > 0);
		assert(q);
		//MARKER("Query=[%s]\n",q);
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
		if (1 == gotCount)
		{
			size_t sz = 0;
			char const * strCheck = NULL;
			char * strCP = NULL;
			/** The following two blocks must behave equivalently, except for
			    how they copy (or not) the underlying string ... */
#if 1
			rc = res->api->get_string_ndx(res, 2, &strCheck, &sz);
#else
			rc = db_wrap_result_string_copy_ndx(res, 2, &strCP, &sz);
			strCheck = strCP;
#endif
			assert(0 == rc);
			assert(sz > 0);
			assert(0 == strcmp( strCheck, strVal) );
			/*MARKER("Read string [%s]\n",strCheck);*/
			if (NULL != strCP) free( strCP );
		}
	}
	assert(gotCount == count);
	assert(DB_WRAP_E_DONE == rc);
	res->api->finalize(res);

	// FIXME: add reset() to the result API.

	/**
	   Now try fetching some values...
	*/



	if (0)
	{
		FIXME("get-double is not working. Not sure why.\n");
		char const * dblSql = "select vdbl from t order by vint desc limit 1";
		// not yet working. don't yet know why
		double doubleGet = -1.0;
		rc = db_wrap_query_double(wr, dblSql, strlen(dblSql), &doubleGet);
		MARKER("doubleGet=%lf\n",doubleGet);
		assert(0 == rc);
		assert(11.0 == doubleGet);
	}

	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);

	int32_t intGet = -1;
	const int32_t intExpect = count;

#if 1
#if 0
	dbi_result dbires = db_wrap_dbi_result(res);
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#elif 0
	dbi_result dbires = (dbi_result)res->impl.data;
	assert(dbi_result_next_row( dbires) );
	intGet = dbi_result_get_int_idx(dbires, 1);
#else
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


}

void test_mysql_1()
{
	db_wrap * wr = NULL;
	int rc = db_wrap_dbi_init2("mysql", &paramMySql, &wr);
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
	int rc = db_wrap_dbi_init2("sqlite3", &paramSqlite, &wr);
	assert(0 == rc);
	assert(wr);
	char const * dbdir = getenv("PWD");
	rc = wr->api->option_set(wr, "sqlite3_dbdir", dbdir);
	assert(0 == rc);
	rc = wr->api->connect(wr);
	assert(0 == rc);
	char const * errmsg = NULL;
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

static void show_help(char const * appname)
{
	printf("Usage:\n\t%s [-s] [-m] [-t]\n",appname);
	puts("Options:");
	puts("\t-t = use non-temporary tables for tests. Will fail if the tables already exist.");
	puts("\t-m = disable mysql test.");
	puts("\t-s = disable sqlite3 test.");
}

int main(int argc, char const ** argv)
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		char const * arg = argv[i];
		if (0 == strcmp("-t", arg) )
		{
			ThisApp.useTempTables = false;
			continue;
		}
		else if (0 == strcmp("-s", arg) )
		{
			ThisApp.testSQLite3 = false;
			continue;
		}
		else if (0 == strcmp("-m", arg) )
		{
			ThisApp.testMySQL = false;
			continue;
		}
		else if ((0 == strcmp("-?", arg))
			     || (0 == strcmp("--help", arg)) )
		{
			show_help(argv[0]);
			return 1;
		}
	}

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
	if (ThisApp.testMySQL) test_mysql_1();
	if (ThisApp.testSQLite3) test_sqlite_1();
	MARKER("If you got this far, it worked.\n");
	return 0;
}
