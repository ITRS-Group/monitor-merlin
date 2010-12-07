#if !defined(_MERLIN_DB_WRAP_DBI_H_INCLUDED)
#define _MERLIN_DB_WRAP_DBI_H_INCLUDED 1

#include "db_wrap.h"
#include <dbi/dbi.h>

/**
   Initializes a db_wrap object using a libdbi backend.

   conn must be an initalized libdbi connection object.  On success,
   ownership of that connection object is passed to the returned
   object, and it will be closed when the returned object is
   finalized.

   param must be non-null and must contain connection parameters for
   the given connection object. tgt must be non-null and *tgt must
   point to NULL.

   On success 0 is returned, *tgt is assigned to the wrapper object,
   which now owns conn and which the caller must eventually free by
   calling (*tgt)->finalize(*tgt).

   The connection is not opened by this function: call tgt->api->connect(tgt)
   to do that.
*/
int db_wrap_dbi_init(dbi_conn conn, db_wrap_conn_params const * param, db_wrap ** tgt);

/**
   A temporary function for use only while porting the dbi-using bits
   to db_wrap. If db was created by the DBI-based driver then its
   dbi-native handle is returned. That object is owned by the
   db_wrap_result object and MUST NOT be destroyed by the caller.

   This function will go away once the port is complete.
*/
dbi_conn db_wrap_dbi_conn(db_wrap * db);


/**
   A temporary function for use only while porting the dbi-using bits
   to db_wrap. If db was created by the DBI-based driver then its
   dbi-native result is returned. That object is owned by the
   db_wrap_result object and MUST NOT be destroyed by the caller.

   This function will go away once the port is complete.
*/
dbi_result db_wrap_dbi_result(db_wrap_result * db);

#endif /* _MERLIN_DB_WRAP_DBI_H_INCLUDED */
