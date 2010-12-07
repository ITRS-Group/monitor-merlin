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
/** Signifies an allocation error. */
DB_WRAP_E_ALLOC_ERROR,
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
	   A destructor for the data member, with the same semantics as
	   free(). Whether or not it actually uses free() is up to the
	   implementation, but the semantics are the same as free().

	   db_wrap::finalize() implementations must, if this member is not
	   NULL, call self->impl.dtor(self->impl.data) from their
	   finalize() member.
	*/
	void (*dtor)(void *);

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

/**
   Wrapper for a database connection object. These objects must be
   initialized using a backend-specific initialization/constructor
   function and freed using their finalize() member.
*/
struct db_wrap
{
	/** Must connect to the underlying database and return 0 on succes. */
	int (*connect)(struct db_wrap * db);
	/**
	   Must quote the first len bytes of the given string as SQL,
	   write the quoted string to *src, and return the number of bytes
	   encoded by this function. If this function returns 0 then the
	   client must not use *str, otherwise he must free it using
	   free_string(). Implementations are free to use a custom
	   allocator, which is why the client MUST use free_string() to
	   free the string.

	   Implementations may optionally stream (len==0) as an indicator
	   that they should use strlen() to count the length of src.

	*/
	size_t (*sql_quote)(struct db_wrap * db, char const * src, size_t len, char ** dest);
	/**
	   Frees a string allocated by sql_quote(). Results are undefined if the string
	   came from another source.
	*/
	int (*free_string)(struct db_wrap * db, char *);
	/**
	   Must initialize a result object for the given db from the first
	   len bytes of the given sql, and populte the given result object
	   with the results.  Must return 0 on success, non-zero on error.

	   On success the caller must eventually clean up the result with
	   result->finalize(result).

	   Implementations may optionally stream (len==0) as an indicator
	   that they should use strlen() to count the length of src.
	*/
	int (*query_result)(struct db_wrap * db, char const * sql, size_t len, struct db_wrap_result ** tgt);
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
	   connection, writing the bytes to *dest and the length to
	   *len. Returns 0 on success.  On error the result should not be
	   used by the caller. On success, the caller must free the string
	   using free_string().
	*/
	int (*error_message)(struct db_wrap * db, char ** dest, size_t * len);
	/**
	   Sets a driver-specific option to the given value. The exact
	   type of val is driver-specific, and the client must be sure to
	   pass a pointer to the proper type.

	   Returns 0 on success.
	*/
	int (*option_set)(struct db_wrap * db, char const * key, void const * val);
	/**
	   Gets a driver-specific option and assigns its value to
	   *val. The exact type of val is driver-specific, and the client
	   must be sure to pass a pointer to the proper type.

	   Returns 0 on success.
	*/
	int (*option_get)(struct db_wrap * db, char const * key, void * val);
	/**
	   Must free up any dynamic resources used by db, but must not
	   free db itself. Not all backends will have a distinction betwen
	   cleanup() and finalize(), but some will.

	   Implementations must call impl.dtor(impl.data). They may, IFF
	   they will only be dynamically allocated, defer that call to the
	   finalize() implementation, but such an implementation may not
	   work with stack- or custom-allocated db_wrap objects.
	*/
	int (*cleanup)(struct db_wrap * db);
	/**
	   Must call cleanup() and then free the db object using a
	   mechanism appropriate for its allocation.
	*/
	int (*finalize)(struct db_wrap * db);

	/**
	   Implementation-specific private details.
	*/
	db_wrap_impl impl;
};
/** Convenience typedef. */
typedef struct db_wrap db_wrap;
/** Empty-initialized db_wrap object. */
extern const db_wrap db_wrap_empty;
/** Empty-initialized db_wrap object. */
#define db_wrap_empty_m { \
			NULL/*connect*/,\
			NULL/*sql_quote*/,\
			NULL/*free_string*/,              \
			NULL/*query_result*/,\
			NULL/*error_message*/,\
			NULL/*option_set*/,\
			NULL/*option_get*/,\
			NULL/*cleanup*/,\
			NULL/*finalize*/,\
			db_wrap_impl_empty_m \
			}

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
	 */
	int (*get_int32_ndx)(db_wrap_result * self, int ndx, int32_t * val);

	/**
	   Must fetch an integer value at the given query index position (0-based!),
	   write its value to *val, and return 0 on success.
	 */
	int (*get_int64_ndx)(db_wrap_result * self, int ndx, int64_t * val);

	/**
	   Must fetch an double value at the given query index position (0-based!),
	   write its value to *val, and return 0 on success.
	 */
	int (*get_double_ndx)(db_wrap_result * self, int ndx, double * val);

	/**
	   Must fetch a string value at the given query index position
	   (0-based!), write its value to *val, write its length to *len,
	   and return 0 on success. The function may allocate memory for
	   the string, and may use a custom memory source.  If *len is
	   greater than 0 after this call then the caller must free the
	   string by calling free_string().
	 */
	int (*get_string_ndx)(db_wrap_result * self, int ndx, char ** val, size_t * len);

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
	char const * user;
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
