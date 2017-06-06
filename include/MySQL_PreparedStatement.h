#ifndef CLASS_MYSQL_PREPARED_STATEMENT_H
#define CLASS_MYSQL_PREPARED_STATEMENT_H

#include "proxysql.h"
#include "cpp.h"

/*
One of the main challenge in handling prepared statement (PS) is that a single
PS could be executed on multiple backends, and on each backend it could have a
different stmt_id.
For this reason ProxySQL returns to the client a stmt_id generated by the proxy
itself, and internally maps client's stmt_id with the backend stmt_id.

The implementation in ProxySQL is, simplified, the follow:
* when a client sends a MYSQL_COM_STMT_PREPARE, ProxySQL executes it to one of
  the backend
* the backend returns a stmt_id. This stmt_id is NOT returned to the client. The
  stmt_id returned from the backend is stored in MySQL_STMTs_local(), and
  MySQL_STMTs_local() is responsible for mapping the connection's MYSQL_STMT
  and a global_stmt_id
* the global_stmt_id is the stmt_id returned to the client
* the global_stmt_id is used to locate the relevant MySQL_STMT_Global_info() in
  MySQL_STMT_Manager()
* MySQL_STMT_Global_info() stores all metadata associated with a PS
* MySQL_STMT_Manager() is responsible for storing all MySQL_STMT_Global_info()
  in global structures accessible and shareble by all threads.

To summarie the most important classes:
* MySQL_STMT_Global_info() stores all metadata associated with a PS
* MySQL_STMT_Manager() stores all the MySQL_STMT_Global_info(), indexes using
  a global_stmt_id that iis the stmt_id generated by ProxySQL and returned to
  the client
* MySQL_STMTs_local() associate PS located in a backend connection to a
  global_stmt_id
*/

// class MySQL_STMT_Global_info represents information about a MySQL Prepared Statement
// it is an internal representation of prepared statement
// it include all metadata associated with it

#define PROXYSQL_STMT_V14

class MySQL_STMT_Global_info {
	private:
	void compute_hash();
	public:
	uint64_t digest;
	MYSQL_COM_QUERY_command MyComQueryCmd;
	char * digest_text;
	uint64_t hash;
	char *username;
	char *schemaname;
	char *query;
	unsigned int query_length;
	unsigned int hostgroup_id;
	int ref_count_client;
	int ref_count_server;
#ifndef PROXYSQL_STMT_V14
	uint32_t statement_id;
#else
	uint64_t statement_id;
#endif
	uint16_t num_columns;
	uint16_t num_params;
	uint16_t warning_count;
	MYSQL_FIELD **fields;
	struct {
		int cache_ttl;
		int timeout;
		int delay;
	} properties;
	bool is_select_NOT_for_update;
	MYSQL_BIND **params; // seems unused (?)
#ifndef PROXYSQL_STMT_V14
	MySQL_STMT_Global_info(uint32_t id, unsigned int h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, uint64_t _h);
#else
	MySQL_STMT_Global_info(uint64_t id, unsigned int h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, uint64_t _h);
#endif
	~MySQL_STMT_Global_info();
};


// stmt_execute_metadata_t represent metadata required to run STMT_EXECUTE
class stmt_execute_metadata_t {
	public:
	uint32_t size;
	uint32_t stmt_id;
	uint8_t flags;
	uint16_t num_params;
	MYSQL_BIND *binds;
	my_bool *is_nulls;
	unsigned long *lengths;
	void *pkt;
	stmt_execute_metadata_t() {
		binds=NULL;
		is_nulls=NULL;
		lengths=NULL;
		pkt=NULL;
	}
	~stmt_execute_metadata_t() {
		if (binds)
			free(binds);
		if (is_nulls)
			free(is_nulls);
		if (lengths)
			free(lengths);
	}
};


typedef struct _stmt_long_data_t {
	uint32_t stmt_id;
	uint16_t param_id;
	void *data;
	unsigned long size;
} stmt_long_data_t;


class StmtLongDataHandler {
	private:
	PtrArray *long_datas;
	public:
	StmtLongDataHandler();
	~StmtLongDataHandler();
	unsigned int reset(uint32_t _stmt_id);
	bool add(uint32_t _stmt_id, uint16_t _param_id, void *_data, unsigned long _size);
	void *get(uint32_t _stmt_id, uint16_t _param_id, unsigned long **_size);
};

// server side, metadata related to STMT_EXECUTE are stored in MYSQL_STMT itself
// client side, they are stored in stmt_execute_metadata_t
// MySQL_STMTs_meta maps stmt_execute_metadata_t with stmt_id
class MySQL_STMTs_meta {
	private:
	unsigned int num_entries;
	std::map<uint32_t, stmt_execute_metadata_t *> m;
	public:
	MySQL_STMTs_meta() {
		num_entries=0;
	}
	~MySQL_STMTs_meta() {
		// FIXME: destructor not there yet
		for (std::map<uint32_t, stmt_execute_metadata_t *>::iterator it=m.begin(); it!=m.end(); ++it) {
			stmt_execute_metadata_t *sem=it->second;
			delete sem;
		}
	}
	// we declare it here to be inline
	void insert(uint32_t global_statement_id, stmt_execute_metadata_t *stmt_meta) {
		std::pair<std::map<uint32_t, stmt_execute_metadata_t *>::iterator,bool> ret;
		ret=m.insert(std::make_pair(global_statement_id, stmt_meta));
		if (ret.second==true) {
			num_entries++;
		}
	}
	// we declare it here to be inline
	stmt_execute_metadata_t * find(uint32_t global_statement_id) {
		auto s=m.find(global_statement_id);
		if (s!=m.end()) {	// found
			return s->second;
		}
		return NULL;	// not found
	}

	void erase(uint32_t global_statement_id) {
		auto s=m.find(global_statement_id);
		if (s!=m.end()) { // found
			stmt_execute_metadata_t *sem=s->second;
			delete sem;
			num_entries--;
			m.erase(s);
		}
	}
};

#ifndef PROXYSQL_STMT_V14
// class MySQL_STMTs_local associates a global statement ID with a local statement ID for a specific connection

class MySQL_STMTs_local {
	private:
	unsigned int num_entries;
	bool is_client;
	std::map<uint32_t, uint32_t> client_stmt_to_global_id;
	std::map<uint32_t, MYSQL_STMT *> m;
	public:
	MySQL_Session *sess;
	MySQL_STMTs_local(bool _ic) {
		sess=NULL;
		is_client=_ic;
		num_entries=0;
	}
	void set_is_client(MySQL_Session *_s) {
		sess=_s;
		is_client=true;
	}
	~MySQL_STMTs_local();
	void insert(uint32_t global_statement_id, MYSQL_STMT *stmt);

	// we declare it here to be inline
	MYSQL_STMT * find(uint32_t global_statement_id) {
		auto s=m.find(global_statement_id);
		if (s!=m.end()) {	// found
			return s->second;
		}
		return NULL;	// not found
	}
	uint32_t generate_new_stmt_id(uint32_t global_statement_id);
	uint32_t find_original_id(uint32_t client_stmt_id);
	bool exists(uint32_t global_statement_id) {
		auto s=m.find(global_statement_id);
		if (s!=m.end()) {	// found
			return true;
		}
		return false;	// not found
	}
	bool erase(uint32_t global_statement_id);
	uint64_t compute_hash(unsigned int hostgroup, char *user, char *schema, char *query, unsigned int query_length);
	unsigned int get_num_entries() { return num_entries; }
};

class MySQL_STMT_Manager {
	private:
	uint32_t next_statement_id;
	rwlock_t rwlock;
	std::map<uint32_t, MySQL_STMT_Global_info *> m;	// map using statement id
	std::map<uint64_t, MySQL_STMT_Global_info *> h;	// map using hashes
	std::stack<uint32_t> free_stmt_ids;
	public:
	MySQL_STMT_Manager();
	~MySQL_STMT_Manager();
	uint32_t generate_new_stmt_id() {
		return __sync_add_and_fetch(&next_statement_id,1);
	}
	int ref_count(uint32_t statement_id, int cnt, bool lock, bool is_client);
	MySQL_STMT_Global_info * add_prepared_statement(bool *is_new, unsigned int h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, bool lock=true);
	MySQL_STMT_Global_info * add_prepared_statement(bool *is_new, unsigned int h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, int _cache_ttl, int _timeout, int _delay, bool lock=true);
	MySQL_STMT_Global_info * find_prepared_statement_by_stmt_id(uint32_t id, bool lock=true);
	MySQL_STMT_Global_info * find_prepared_statement_by_hash(uint64_t hash, bool lock=true);
	uint32_t total_prepared_statements() { return next_statement_id-1; }
	void active_prepared_statements(uint32_t *unique, uint32_t *total);
};

#else // PROXYSQL_STMT_V14
class MySQL_STMTs_local_v14 {
	private:
	bool is_client_;
	// this map associate client_stmt_id to global_stmt_id : this is used only for client connections
	std::map<uint32_t, uint64_t> client_stmt_to_global_ids;
	// this multimap associate global_stmt_id to client_stmt_id : this is used only for client connections
	std::multimap<uint64_t, uint32_t> global_stmt_to_client_ids;

	// this map associate backend_stmt_id to global_stmt_id : this is used only for backend connections
	std::map<uint32_t, uint64_t> backend_stmt_to_global_ids;
	// this map associate global_stmt_id to backend_stmt_id : this is used only for backend connections
	std::map<uint64_t, uint32_t> global_stmt_to_backend_ids;

	std::map<uint64_t, MYSQL_STMT *> global_stmt_to_backend_stmt;

	std::stack<uint32_t> free_client_ids;
	uint32_t local_max_stmt_id;
	public:
	MySQL_Session *sess;
	MySQL_STMTs_local_v14(bool _ic) {
		local_max_stmt_id = 0;
		sess = NULL;
		is_client_ = _ic;
	}
	void set_is_client(MySQL_Session *_s) {
		sess=_s;
		is_client_ = true;
	}
	~MySQL_STMTs_local_v14();
	bool is_client() {
		return is_client_;
	}
	void backend_insert(uint64_t global_statement_id, MYSQL_STMT *stmt);
	uint64_t compute_hash(unsigned int hostgroup, char *user, char *schema, char *query, unsigned int query_length);
	unsigned int get_num_backend_stmts() { return backend_stmt_to_global_ids.size(); }
	uint32_t generate_new_client_stmt_id(uint64_t global_statement_id);
	uint64_t find_global_stmt_id_from_client(uint32_t client_stmt_id);
	bool client_close(uint32_t client_statement_id);
	MYSQL_STMT * find_backend_stmt_by_global_id(uint32_t global_statement_id) {
		auto s=global_stmt_to_backend_stmt.find(global_statement_id);
		if (s!=global_stmt_to_backend_stmt.end()) {	// found
			return s->second;
		}
		return NULL;	// not found
	}
};

class MySQL_STMT_Manager_v14 {
	private:
	uint64_t next_statement_id;
	pthread_rwlock_t rwlock_;
	std::map<uint64_t, MySQL_STMT_Global_info *> map_stmt_id_to_info;	// map using statement id
	std::map<uint64_t, MySQL_STMT_Global_info *> map_stmt_hash_to_info;	// map using hashes
	public:
	MySQL_STMT_Manager_v14();
	~MySQL_STMT_Manager_v14();
	MySQL_STMT_Global_info * find_prepared_statement_by_hash(uint64_t hash, bool lock=true);
	MySQL_STMT_Global_info * find_prepared_statement_by_stmt_id(uint64_t id, bool lock=true);
	void rdlock() { pthread_rwlock_rdlock(&rwlock_); }
	void wrlock() { pthread_rwlock_wrlock(&rwlock_); }
	void unlock() { pthread_rwlock_unlock(&rwlock_); }
	void ref_count_client(uint64_t _stmt, int _v, bool lock=true);
	void ref_count_server(uint64_t _stmt, int _v, bool lock=true);
	MySQL_STMT_Global_info * add_prepared_statement(unsigned int h, char *u, char *s, char *q, unsigned int ql, MYSQL_STMT *stmt, int _cache_ttl, int _timeout, int _delay, bool lock=true);
	void get_metrics(uint64_t *c_unique, uint64_t *c_total, uint64_t *stmt_max_stmt_id, uint64_t *cached, uint64_t *s_unique, uint64_t *s_total);
};
#endif // PROXYSQL_STMT_V14

#endif /* CLASS_MYSQL_PREPARED_STATEMENT_H */
