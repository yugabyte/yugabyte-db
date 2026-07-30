#pragma once

#include "duckdb.hpp"

namespace pgduckdb {

namespace ddb {
bool DidWrites();
bool DidWrites(duckdb::ClientContext &context);
} // namespace ddb

class DuckDBManager {
public:
	static inline bool
	IsInitialized() {
		return manager_instance.database != nullptr;
	}

	static inline DuckDBManager &
	Get() {
		if (!manager_instance.database) {
			manager_instance.Initialize();
		}
		return manager_instance;
	}

	static void
	InvalidateDuckDBSecretsIfInitialized() {
		// Only invalidate the secrets if the database is initialized.
		if (IsInitialized()) {
			manager_instance.InvalidateDuckDBSecrets();
		}
	}

	static duckdb::unique_ptr<duckdb::Connection> CreateConnection();
	static duckdb::Connection *GetConnection(bool force_transaction = false);
	static duckdb::Connection *GetConnectionUnsafe();

	inline const std::string &
	GetDefaultDBName() const {
		return default_dbname;
	}

	inline void
	InvalidateDuckDBSecrets() {
		secrets_valid = false;
	}

	inline duckdb::DuckDB &
	GetDatabase() {
		return *database;
	}

	static void Reset();

private:
	DuckDBManager()
	    : extensions_table_current_seq(0), database(nullptr), connection(nullptr), default_dbname("<!UNSET!>"),
	      secrets_valid(false) {
	}

	DuckDBManager(const DuckDBManager &) = delete;
	DuckDBManager &operator=(const DuckDBManager &) = delete;

	static DuckDBManager manager_instance;

	void Initialize();

	void InitializeDatabase();

	void LoadSecrets(duckdb::ClientContext &);
	void DropSecrets(duckdb::ClientContext &);
	void LoadExtensions(duckdb::ClientContext &);
	void InstallExtensions(duckdb::ClientContext &);
	void LoadFunctions(duckdb::ClientContext &);
	void RefreshConnectionState(duckdb::ClientContext &);

	inline bool
	IsExtensionsSeqLessThan(int64_t seq) const {
		return extensions_table_current_seq < seq;
	}

	inline void
	UpdateExtensionsSeq(int64_t seq) {
		extensions_table_current_seq = seq;
	}

	int64_t extensions_table_current_seq;
	/*
	 * FIXME: Use a unique_ptr instead of a raw pointer. For now this is not
	 * possible though, as the MotherDuck extension causes an ABORT when the
	 * DuckDB database its destructor is run at the exit of the process.  This
	 * then in turn crashes Postgres, which we obviously dont't want. Not
	 * running the destructor also doesn't really have any downsides, as the
	 * process is going to die anyway. It's probably even a tiny bit more
	 * efficient not to run the destructor at all. But we should still fix
	 * this, because running the destructor is a good way to find bugs (such
	 * as the one reported in #279).
	 */
	duckdb::DuckDB *database;
	duckdb::unique_ptr<duckdb::Connection> connection;
	std::string default_dbname;
	bool secrets_valid;
};

} // namespace pgduckdb
