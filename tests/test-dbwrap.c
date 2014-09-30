#include "config.h"
#include "db_wrap.h"
#include <assert.h>

#include <stdio.h> /*printf()*/
#include <stdlib.h> /*getenv(), atexit()*/
#include <string.h> /* strlen() */
#include <inttypes.h> /* PRIuXX macros */
#include <stdbool.h>
#define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf
#define FIXME(X) MARKER("FIXME: " X)

static struct {
	db_wrap_conn_params mysql;
	db_wrap_conn_params sqlite3;
} ConnParams = {
	db_wrap_conn_params_empty_m,
	db_wrap_conn_params_empty_m,
};


static struct {
	bool useTempTables;
	bool testMySQL;
	bool testSQLite3;
} ThisApp = {
	true/*useTempTables*/,
	false/*testMySQL*/,
	false/*testSQLite3*/,
};

static void show_errinfo_impl(db_wrap *wr, int rc, unsigned int line)
{
	if (0 != rc) {
		char const *errStr = NULL;
		int dbErrCode = 0;
		wr->api->error_info(wr, &errStr, NULL, &dbErrCode);
		MARKER("line #%u, DB driver error info: db_wrap rc=%d, back-end error code=%d [%s]\n",
		       line, rc, dbErrCode, errStr);
	}
}
#define show_errinfo(WR,RC) show_errinfo_impl(WR, RC, __LINE__)

static void test_dbwrap_generic(char const *driver, db_wrap *wr)
{
	MARKER("Running generic tests: [%s]\n", driver);

	const bool isMysql = (NULL != strstr(driver, "mysql"));
	const bool isSqlite = (NULL != strstr(driver, "sqlite"));

	if (isSqlite == isMysql) {
		// THIS IS ONLY HERE TO AVOID 'UNUSED VARIABLE' WARNINGS!
	}

#define TABLE_DEF                                               \
	"table t(vint integer, vdbl float(12), vstr varchar(32))"
	char const *sql = NULL;
	if (ThisApp.useTempTables) {
		sql = ("create temporary " TABLE_DEF);
	} else {
		sql = "create " TABLE_DEF;
		;
	}
#undef TABLE_DEF
	assert(NULL != sql);
	db_wrap_result *res = NULL;
	int rc;

#if 0
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	show_errinfo(wr, rc);
	assert(0 == rc);
	assert(NULL != res);
	//MARKER("dbi_wrap_result@%p, dbi_result@%p\n",(void const *)res, res->impl.data);
	rc = res->api->finalize(res);
	show_errinfo(wr, rc);
	assert(0 == rc);
	res = NULL;
#else
	rc = db_wrap_query_exec(wr, sql, strlen(sql));
	show_errinfo(wr, rc);
	assert(0 == rc);
#endif

	size_t i;
	const size_t count = 10;
	char const *strVal = "hi, world";
	for (i = 1; i <= count; ++i) {
		char *q = NULL;
		rc = asprintf(&q, "insert into t (vint, vdbl, vstr) values(%lu,%2.1lf,'%s')",
		              i, (i * 1.1), strVal);
		assert(rc > 0);
		assert(q);
		res = NULL;
		rc = wr->api->query_result(wr, q, strlen(q), &res);
		show_errinfo(wr, rc);
		//MARKER("Query rc=[%d]  [%s]\n",rc, q);
		free(q);
		assert(0 == rc);
		assert(NULL != res);
		rc = res->api->finalize(res);
		assert(0 == rc);
		show_errinfo(wr, rc);
		res = NULL;
	}

	sql =
	    "select * from t order by vint desc"
	    ;
	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);
	//assert(res->impl.data == db_wrap_dbi_result(res));

	/* ensure that stepping acts as expected. */
	size_t gotCount = 0;
	while (0 == (rc = res->api->step(res))) {
		++gotCount;
		if (1 == gotCount) {
			size_t sz = 0;
			char const *strCheck = NULL;
			char *strCP = NULL;
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
			assert(0 == strcmp(strCheck, strVal));
			/*MARKER("Read string [%s]\n",strCheck);*/
			if (NULL != strCP) { free(strCP); }
		}
	}
	assert(gotCount == count);
	assert(DB_WRAP_E_DONE == rc);
	res->api->finalize(res);

	{
		res = NULL;
		rc = wr->api->query_result(wr, sql, strlen(sql), &res);
		assert(0 == rc);
		assert(NULL != res);
		while (0 == res->api->step(res)) {}
		size_t rowCount = 0;
		res->api->num_rows(res, &rowCount);
		res->api->finalize(res);
		MARKER("Row count=%u, expecting %u. sql=[%s]\n", (unsigned int)rowCount, (unsigned int)count, sql);
		assert(rowCount == count);
	}

	// FIXME: add reset() to the result API.

	/**
	   Now try fetching some values...
	*/


	const bool doCountTest = (NULL == strstr(driver, "sqlite"));
	if (!doCountTest) {
		MARKER("WARNING: skipping count(*) test because the libdbi sqlite driver apparently doesn't handle the numeric type properly!\n");
	} else {
		sql = "select count(*) as C from t";
		res = NULL;
		rc = wr->api->query_result(wr, sql, strlen(sql), &res);
		assert(0 == rc);
		assert(NULL != res);
		rc = res->api->step(res);
		assert(0 == rc);
		typedef int64_t CountType;
		CountType ival = -1;
		rc =
		    //res->api->get_int32_ndx(res, 0, &ival)
		    res->api->get_int64_ndx(res, 0, &ival)
		    /*
		      DAMN: the libdbi impls behave differently here: on some platforms
		      count(*) will be (u)int32 and on some 64. The libdbi drivers
		      are horribly pedantic here and require that we know exactly how
		      big the integer is.

		      http://www.mail-archive.com/libdbi-users@lists.sourceforge.net/msg00126.html

		      This particular test works for me on mysql but not sqlite3. It also
		      possibly fails on mysql 32-bit (untested).
		    */
		    ;
		MARKER("Select COUNT(*)/step/fetch rc=%d, ival=%ld, expecting=%ld\n", rc, (long)ival, (long)gotCount);
		assert(0 == rc);
		assert((size_t)ival == gotCount);
		res->api->finalize(res);
		res = NULL;
		//assert( res->impl.data == db_wrap_dbi_result(res) );
	}


	if (!isSqlite) {
		/*
		  FIXME: get-double appears to be broken at the libdbi level for sqlite.
		 */
		char const *dblSql = "select vdbl from t order by vint desc limit 1";

		// not yet working. don't yet know why
		double doubleGet = -1.0;
		rc = db_wrap_query_double(wr, dblSql, strlen(dblSql), &doubleGet);
		if (0 != rc) {
			char const *errStr = NULL;
			int driverRc = 0;
			wr->api->error_info(wr, &errStr, NULL, &driverRc);
			MARKER("doubleGet: rc=%d, driverRc=%d (%s), val=%lf\n", rc, driverRc, errStr, doubleGet);
		}
		assert(0 == rc);
		assert(11.0 == doubleGet);
	} else {
		MARKER("WARNING: the fetch-double test has been disabled!\n");
	}

	sql =
	    "select * from t order by vint desc"
	    //"select count(*) as C from t"
	    ;
	res = NULL;
	rc = wr->api->query_result(wr, sql, strlen(sql), &res);
	assert(0 == rc);
	assert(NULL != res);

	int32_t intGet = -1;
	const int32_t intExpect = count;

#if 1
#if 0
	dbi_result dbires = db_wrap_dbi_result(res);
	assert(dbi_result_next_row(dbires));
	intGet = dbi_result_get_int_idx(dbires, 1);
#elif 0
	dbi_result dbires = (dbi_result)res->impl.data;
	assert(dbi_result_next_row(dbires));
	intGet = dbi_result_get_int_idx(dbires, 1);
#else
	rc = res->api->step(res);
	assert(0 == rc);
	rc = res->api->get_int32_ndx(res, 0, &intGet);
	assert(0 == rc);
#endif
	//MARKER("got int=%d, expected=%d\n",intGet, intExpect);
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

static void test_mysql_1(void)
{
#ifndef DB_WRAP_CONFIG_ENABLE_LIBDBI
	assert(0 && "ERROR: dbi:mysql support not compiled in!");
#else
	db_wrap *wr = NULL;
	int rc = db_wrap_driver_init("dbi:mysql", &ConnParams.mysql, &wr);
	assert(0 == rc);
	assert(wr);
	rc = wr->api->connect(wr);
	assert(0 == rc);

	char *sqlCP = NULL;
	char const *sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	/* ACHTUNG: what libdbi does here with the escaping is NOT SQL STANDARD. */
	assert(0 == strcmp("'hi, \\'world\\''", sqlCP));
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	test_dbwrap_generic("dbi:mysql", wr);

	rc = wr->api->finalize(wr);
	assert(0 == rc);
#endif
}

static void test_sqlite_1(void)
{
#ifndef DB_WRAP_CONFIG_ENABLE_LIBDBI
	assert(0 && "ERROR: dbi:sqlite3 support not compiled in!");
#else
	db_wrap *wr = NULL;
	int rc = db_wrap_driver_init("dbi:sqlite3", &ConnParams.sqlite3, &wr);
	assert(0 == rc);
	assert(wr);
	char const *dbdir = getenv("PWD");
	rc = wr->api->option_set(wr, "sqlite3_dbdir", dbdir);
	assert(0 == rc);
	rc = wr->api->connect(wr);
	assert(0 == rc);
	char const *errmsg = NULL;
	int dbErrno = 0;
	rc = wr->api->error_info(wr, &errmsg, NULL, &dbErrno);
	assert(0 == rc);
	assert(NULL == errmsg);

	char *sqlCP = NULL;
	char const *sql = "hi, 'world'";
	size_t const sz = strlen(sql);
	size_t const sz2 = wr->api->sql_quote(wr, sql, sz, &sqlCP);
	assert(0 != sz2);
	assert(sz != sz2);
	assert(0 == strcmp("'hi, ''world'''", sqlCP));
	rc = wr->api->free_string(wr, sqlCP);
	assert(0 == rc);

	sql = NULL;
	rc = wr->api->option_get(wr, "sqlite3_dbdir", &sql);
	assert(0 == rc);
	assert(0 == strcmp(sql, dbdir));

	test_dbwrap_generic("dbi:sqlite3", wr);


	rc = wr->api->finalize(wr);
	assert(0 == rc);
#endif
}

static void show_help(char const *appname)
{
	printf("Usage:\n\t%s [-s] [-m] [-t]\n", appname);
	puts("Options:");
	puts("\t-t = use non-temporary tables for tests. Will fail if the tables already exist.");
	puts("\t-m = enables mysql test.");
	puts("\t-s = enables sqlite3 test.");
	puts("\t-h HOSTNAME = sets remote host name for some tests.");
	putchar('\n');
}

int main(int argc, char const **argv)
{
	int i;
	int testCount = 0;
	char const *dbhost = "localhost";
	for (i = 1; i < argc; ++i) {
		char const *arg = argv[i];
		if (0 == strcmp("-t", arg)) {
			ThisApp.useTempTables = false;
			continue;
		} else if (0 == strcmp("-s", arg)) {
			ThisApp.testSQLite3 = true;
			++testCount;
			continue;
		} else if (0 == strcmp("-m", arg)) {
			ThisApp.testMySQL = true;
			++testCount;
			continue;
		} else if (0 == strcmp("-h", arg)) {
			dbhost = argv[++i];
			continue;
		} else if ((0 == strcmp("-?", arg))
		           || (0 == strcmp("--help", arg))) {
			show_help(argv[0]);
			return 1;
		}
	}

	if (testCount < 1) {
		puts("No test options specified!");
		show_help(argv[0]);
		return 1;
	}
	{
		ConnParams.mysql.host = dbhost;
		ConnParams.mysql.port = 3306;
		ConnParams.mysql.username = "merlin";
		ConnParams.mysql.password = "merlin";
		ConnParams.mysql.dbname = "merlin";
	}
	{
		ConnParams.sqlite3.dbname = "merlin.sqlite";
	}
	if (ThisApp.testMySQL) { test_mysql_1(); }
	if (ThisApp.testSQLite3) { test_sqlite_1(); }
	MARKER("If you got this far, it worked.\n");
	return 0;
}
