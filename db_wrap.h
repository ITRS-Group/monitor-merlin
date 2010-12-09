#if !defined(_MERLIN_DB_WRAP_H_INCLUDED)
#define _MERLIN_DB_WRAP_H_INCLUDED 1


#include <stdint.h> /* standard fixed-sized integers */
#include <stddef.h> /* size_t on Linux */


enum db_wrap_constants
{
/**
   The non-error code.
*/
DB_WRAP_E_OK = 0,
/** Signifies that looping over a result set has successfully
	reached the end of the set.
*/
DB_WRAP_E_DONE,
/** Signifies that some argument is illegal. */
DB_WRAP_E_BAD_ARG,
/** Signifies that some argument is not of the required type. */
DB_WRAP_E_TYPE_ERROR,
/** Signifies an allocation error. */
DB_WRAP_E_ALLOC_ERROR,
/**
	Signifies an unknown error, probably coming from an underying API
	(*ahem*libdbi*ahem*) which cannot, for reasons of its own, tell us
	what went wrong.

*/
DB_WRAP_E_UNKNOWN_ERROR,

/**
   This code signifies that the caller should check the database
   error data via db_wrap_api::error_message().
*/
DB_WRAP_E_CHECK_DB_ERROR,
/** Signifies an unsupported operation. */
DB_WRAP_E_UNSUPPORTED
/*
  TODOs:

  authentication errors, connection errors, etc., etc., etc., etc.
  We don't need to cover every code of every underlying db, but
  we need the basics.
*/
};
typedef enum db_wrap_constants db_wrap_constants;

/**
   Internal implementation data for the db_wrap
   and db_wrap_result APIs. Holds implementation-specific
   data.
*/
struct db_wrap_impl
{
	/**
	   Arbitrary implementation-dependent data.
	*/
	void * data;

	/**
	   Wrapper-specific type ID specifier. May have any internal
	   value, as long as all instances of a given wrapper class have
	   the same value. This is used internally to ensure that the
	   various functions are only passed values of the proper concrete
	   type.
	*/
	void const * typeID;
};
/** Convenience typedef. */
typedef struct db_wrap_impl db_wrap_impl;

/** Empty-initialized db_wrap_impl object. */
#define db_wrap_impl_empty_m {NULL,NULL,NULL}

/** Empty-initialized db_wrap_impl object. */
extern const db_wrap_impl db_wrap_impl_empty;

struct db_wrap_result;

/** Convenience typedef. */
typedef struct db_wrap_result db_wrap_result;

struct db_wrap;
/** Convenience typedef. */
typedef struct db_wrap db_wrap;
/**
   This type holds the "vtbl" (member functions) for db_wrap
   objects. All instances for a given db wrapper back-end share a
   single instance of this class.
*/
struct db_wrap_api
{
	/** Must connect to the underlying database and return 0 on succes. */
	int (*connect)(db_wrap * db);
	/**
	   Must quote the first len bytes of the given string as SQL, add
	   SQL quote characters around it, write the quoted string to
	   *src, and return the number of bytes encoded by this
	   function. If this function returns 0 then the client must not
	   use *str, otherwise he must free it using
	   free_string(). Implementations are free to use a custom
	   allocator, which is why the client MUST use free_string() to
	   free the string.

	   Implementations may optionally stream (len==0) as an indicator
	   that they should use strlen() to count the length of src.

	   ACHTUNG: this is itended for use with individual SQL statement
	   parts, not whole SQL statements.
	*/
	size_t (*sql_quote)(db_wrap * db, char const * src, size_t len, char ** dest);
	/**
	   Frees a string allocated by sql_quote() or
	   error_message(). Results are undefined if the string came from
	   another source. It must return 0 on success. It must, like
	   free(), treat a NULL string gracefully, ignoring it.
	*/
	int (*free_string)(db_wrap * db, char *);
	/**
	   Must initialize a result object for the given db from the first
	   len bytes of the given sql, and populte the given result object
	   with the results.  Must return 0 on success, non-zero on error.

	   On success the caller must eventually clean up the result with
	   result->api->finalize(result).

	   Implementations may optionally stream (len==0) as an indicator
	   that they should use strlen() to count the length of src.
	*/
	int (*query_result)(db_wrap * db, char const * sql, size_t len, struct db_wrap_result ** tgt);
#if 0
	/** these can be implemented generically in terms of query_result(). i think. */
	int (*query_void)(struct db_wrap * db, char const * sql, size_t len);
	int (*query_int32)(struct db_wrap * db, char const * sql, size_t len, int32_t * tgt);
	int (*query_int64)(struct db_wrap * db, char const * sql, size_t len, int64_t * tgt);
	int (*query_double)(struct db_wrap * db, char const * sql, size_t len, double * tgt);
	int (*query_string)(struct db_wrap * db, char const * sql, size_t len, char ** tgt, size_t * tgtLen);
#endif
	/**
	   Must return the last error message associated with the
	   connection, writing the bytes to *dest and the length to *len
	   (if len is not NULL, which implementations should
	   allow). Returns 0 on success.  On error the result should not
	   be used by the caller. On success, the caller must free the
	   string (if it is not NULL) using free_string(). On success
	   *dest may be set to NULL if the driver has no error to report.
	   Some drivers may return other strings on error (sqlite3
	   infamously uses "not an error" for this case).

	   Note that some drivers own their error message strings but we cannot
	   reasonably define their lifetimes in terms of this interface, thus
	   implementations are required to allocate and copy them, and clients
	   are required to free them using db->api->free_string() instead of
	   free() (so that implementations have some leeway in the allocation
	   of the string).
	*/
	int (*error_message)(db_wrap * db, char ** dest, size_t * len);
	/**
	   Sets a driver-specific option to the given value. The exact
	   type of val is driver-specific, and the client must be sure to
	   pass a pointer to the proper type.

	   Returns 0 on success.
	*/
	int (*option_set)(db_wrap * db, char const * key, void const * val);
	/**
	   Gets a driver-specific option and assigns its value to
	   *val. The exact type of val is driver-specific, and the client
	   must be sure to pass a pointer to the proper type.

	   For string options, the caller must pass a (char const **),
	   which will be pointed to the option bytes, which are owned by
	   the wrapper object and have no designated lifetime. To be safe,
	   the caller should copy the bytes if needed beyond the next call
	   to the call into the db.

	   For integer options, the caller must pass a (int*), which will be
	   assigned the option's value by this function.

	   Returns 0 on success.
	*/
	int (*option_get)(db_wrap * db, char const * key, void * val);
	/**
	   Must free up any dynamic resources used by db, but must not
	   free db itself. Not all backends will have a distinction betwen
	   cleanup() and finalize(), but some will.

	   Implementations must call impl.dtor(impl.data). They may, IFF
	   they will only be dynamically allocated, defer that call to the
	   finalize() implementation, but such an implementation may not
	   work with stack- or custom-allocated db_wrap objects.
	*/
	int (*cleanup)(db_wrap * db);
	/**
	   Must call cleanup() and then free the db object using a
	   mechanism appropriate for its allocation.
	*/
	int (*finalize)(db_wrap * db);

};
typedef struct db_wrap_api db_wrap_api;
/**
   Wrapper for a database connection object. These objects must be
   initialized using a backend-specific initialization/constructor
   function and freed using their finalize() member.

   This class defines only the interface. Concrete implementations
   must be provided which provide the features called for by the
   interface.
*/
struct db_wrap
{
	/**
	   The "virtual" member functions of this class. It is illegal for
	   this to be NULL, and all instances for a given database
	   back-end typically share a pointer to the same immutable
	   instance.
	*/
	db_wrap_api const * api;
	/**
	   Implementation-specific private details.
	*/
	db_wrap_impl impl;
};
/** Empty-initialized db_wrap object. */
extern const db_wrap db_wrap_empty;
/** Empty-initialized db_wrap object. */
#define db_wrap_empty_m { \
			NULL/*api*/,\
			db_wrap_impl_empty_m \
			}

/**
   This type holds the "vtbl" (member functions) for
   db_wrap_result objects. All results for a given
   db wrapper back-end share a single instance of
   this class.
*/
struct db_wrap_result_api
{
	/** Must "step" the cursor one position and return:

	- DB_WRAP_E_OK on success.
	- DB_WRAP_E_DONE if we have reached the end without an error.
	- Any other value on error

	FIXME: replace these values with an enum. i HATE these semantics,
	but i can't think of an alternative without adding another member
	function like "is_okay()".

	*/
	int (*step)(db_wrap_result * self);

	/**
	   Must fetch an integer value at the given query index position (0-based!),
	   write its value to *val, and return 0 on success.

	   Reminder: some db API use 0-based indexes for fetching fields
	   by index and some use 1-based. This API uses 0-based because in
	   my experience that is most common (though 1-base is commonly
	   used in bind() APIs and 1-based also makes sense for
	   field-getter APIs).
	 */
	int (*get_int32_ndx)(db_wrap_result * self, unsigned int ndx, int32_t * val);

	/**
	   Must fetch an integer value at the given query index position (0-based!),
	   write its value to *val, and return 0 on success.
	 */
	int (*get_int64_ndx)(db_wrap_result * self, unsigned int ndx, int64_t * val);

	/**
	   Must fetch an double value at the given query index position (0-based!),
	   write its value to *val, and return 0 on success.

	 */
	int (*get_double_ndx)(db_wrap_result * self, unsigned int ndx, double * val);

	/**
	   Must fetch a string value at the given query index position
	   (0-based!), write its value to *val, write its length to *len,
	   and return 0 on success. The function may allocate memory for
	   the string, and may use a custom memory source.  If *len is
	   greater than 0 after this call then the caller must free the
	   string by calling free_string().
	 */
	int (*get_string_ndx)(db_wrap_result * self, unsigned int ndx, char ** val, size_t * len);

	/**
	   Must free the given string, which must have been allocated by
	   get_string_ndx() (or equivalent).
	*/
	int (*free_string)(db_wrap_result * self, char * str);

	/**
	   Must free all resources associated with self and then
	   deallocate self in a manner appropriate to its allocation
	   method.
	*/
	int (*finalize)(db_wrap_result * self);
};
/**
   Convenience typedef.
*/
typedef struct db_wrap_result_api db_wrap_result_api;
/**
   Wraps the basic functionality of "db result" objects, for looping
   over result sets.
*/
struct db_wrap_result
{
	db_wrap_result_api const * api;
	/*
	  TODOs???

	  - cursor reset(). Rarely used.

	  - fetch column by name?

	  - get list of column names? i think(?) this has to come from the
	  prepared statement, not the result?
	*/
	db_wrap_impl impl;
};

/** Empty-initialized db_wrap_result object. */
#define db_wrap_result_empty_m {\
		NULL/*api*/, \
		db_wrap_impl_empty_m/*impl*/        \
	}
/** Empty-initialized db_wrap_result object. */
extern const db_wrap_result db_wrap_result_empty;

/**
   A helper type for db-specific functions which need to take some
   common information in their initialization routine(s).
*/
struct db_wrap_conn_params
{
	char const * host;
	int port;
	char const * username;
	char const * password;
	char const * dbname;
};
typedef struct db_wrap_conn_params db_wrap_conn_params;
/** Empty-initialized db_wrap_conn_params object. */
#define db_wrap_conn_params_empty_m { \
		NULL/*host*/,0/*port*/,NULL/*user*/,NULL/*password*/,NULL/*dbname*/   \
	}
/** Empty-initialized db_wrap_conn_params object. */
extern const db_wrap_conn_params db_wrap_conn_params_empty;
#endif /* _MERLIN_DB_WRAP_H_INCLUDED */
