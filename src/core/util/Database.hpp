#ifndef __UTIL_DATABASE_H__
#define __UTIL_DATABASE_H__

#ifndef __puma
#include <boost/thread.hpp>
#endif

#include <mysql/mysql.h>
#include <iostream>
#include <string>

namespace fail {

	/** \class Database
	 *
	 * Database abstraction layer that handles the database connection
	 * parameters, the database connection and provides different
	 * database access methods
	 */
	class Database {
		MYSQL *handle; // !< The MySQL Database handle
		MYSQL_RES *last_result; // !< Used for mysql_result_free
#ifndef __puma
		boost::mutex m_handle_lock;
#endif

	public:
		/**
		 * Constructor that connects instantly to the database
		 */
		Database(const std::string &username, const std::string &host, const std::string &database);
		~Database() { mysql_close(handle); }

		/**
		 * Get the variant id for a specific variant/benchmark pair,
		 * if it isn't defined in the database (variant table), it is
		 * inserted
		 */
		int get_variant_id(const std::string &variant, const std::string &benchmark);

		/**
		 * Get the fault space pruning method id for a specific
		 * pruning method, if it isn't defined in the database
		 * (fspmethod table), it is inserted.
		 */
		int get_fspmethod_id(const std::string &method);


		/**
		 * Get the raw mysql database handle
		 */
		MYSQL * getHandle() const { return handle; }
		/**
		 * Do a small database query. If get_result is set to false
		 * (MYSQL_RES *)0 or (MYSQL_RES *)1 is given back to indicate
		 * the result of the query. If the result should be fetched
		 * a pointer is returned. This pointer is valid until the next
		 * call to this->query(stmt, true)
		 */
		MYSQL_RES *query(char const *query, bool get_result = false);
		/**
		 * Similar to Database::query, but this should be used for big
		 * queries. The result is not copied instantly from the
		 * database server, but a partial result is returned. Which
		 * behaves as a normal MYSQL_RES pointer.
		 */
		MYSQL_RES *query_stream(char const *query);

		/**
		 * How many rows were affected by the last query
		 */
		my_ulonglong affected_rows();

		/**
		 * Interface to the util/CommandLine.hpp interface. In you
		 * application you first call cmdline_setup(), which adds
		 * different command line options. The cmdline_connect()
		 * function then parses the commandline and connects to the
		 * database.
		 */
		static void cmdline_setup();
		static Database * cmdline_connect();
	};

}

#endif