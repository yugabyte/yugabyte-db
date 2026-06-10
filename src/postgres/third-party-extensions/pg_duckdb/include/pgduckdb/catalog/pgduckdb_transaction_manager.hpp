#pragma once

#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/common/reference_map.hpp"
#include "pgduckdb/pg/declarations.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

class PostgresCatalog;
class PostgresTransaction;

class PostgresTransactionManager : public duckdb::TransactionManager {
public:
	PostgresTransactionManager(duckdb::AttachedDatabase &db_p, PostgresCatalog &catalog);

	duckdb::Transaction &StartTransaction(duckdb::ClientContext &context) override;
	duckdb::ErrorData CommitTransaction(duckdb::ClientContext &context, duckdb::Transaction &transaction) override;
	void RollbackTransaction(duckdb::Transaction &transaction) override;

	void Checkpoint(duckdb::ClientContext &context, bool force = false) override;

private:
	PostgresCatalog &catalog;
	duckdb::mutex transaction_lock;
	duckdb::reference_map_t<duckdb::Transaction, duckdb::unique_ptr<duckdb::Transaction>> transactions;
};

} // namespace pgduckdb
