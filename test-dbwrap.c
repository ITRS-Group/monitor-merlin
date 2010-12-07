#include "db_wrap.h"
#include "db_wrap_dbi.h"
#include <assert.h>

#include <stdio.h>
#define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__); printf


void test1()
{
	db_wrap * wr = NULL;
	db_wrap_conn_params param = db_wrap_conn_params_empty;
	param.host = "localhost";
	param.port = 3306;
	param.user = "vfuc";
	param.password = "consol";
	param.dbname = "repodb";
	dbi_conn conn = dbi_conn_new("mysql");
	assert(conn);
	int rc = db_wrap_dbi_init(conn, &param, &wr);
	assert(0 == rc);
	assert(wr);
	rc = wr->connect(wr);
	assert(0 == rc);
	rc = wr->finalize(wr);
	assert(0 == rc);
}

int main(int argc, char const ** argv)
{
	dbi_initialize(NULL);
	test1();
	dbi_shutdown();
	return 0;
}
