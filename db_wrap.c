#include "db_wrap.h"

const db_wrap_impl db_wrap_impl_empty = db_wrap_impl_empty_m;
const db_wrap db_wrap_empty = db_wrap_empty_m;
const db_wrap_result db_wrap_result_empty = db_wrap_result_empty_m;
const db_wrap_conn_params db_wrap_conn_params_empty = db_wrap_conn_params_empty_m;

#if 0
int db_wrap_query_int32(db_wrap * self, char const * sql, size_t len, int32_t * tgt)
{
	TODO("implement this.");
	return -1;
}
int db_wrap_query_int64(db_wrap * self, char const * sql, size_t len, int64_t * tgt)
{
	TODO("implement this.");
	return -1;
}
int db_wrap_query_double(db_wrap * self, char const * sql, size_t len, double * tgt)
{
	TODO("implement this.");
	return -1;
}
int db_wrap_query_string(db_wrap * self, char const * sql, size_t len, char ** tgt, size_t * tgtLen)
{
	TODO("implement this.");
	return -1;
}
#endif
