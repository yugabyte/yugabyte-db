#include "pgduckdb/catalog/pgduckdb_transaction_manager.hpp"
#include "duckdb/main/client_context.hpp"
#include "pgduckdb/catalog/pgduckdb_transaction.hpp"
#include "pgduckdb/pg/snapshots.hpp"
#include "pgduckdb/pgduckdb_process_lock.hpp"

#include "duckdb/main/attached_database.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

PostgresTransactionManager::PostgresTransactionManager(duckdb::AttachedDatabase &_db_p, PostgresCatalog &_catalog)
    : TransactionManager(_db_p), catalog(_catalog), transaction_lock(), transactions() {
}

duckdb::Transaction &
PostgresTransactionManager::StartTransaction(duckdb::ClientContext &context) {
	auto transaction = duckdb::make_uniq<PostgresTransaction>(*this, context, catalog, GetActiveSnapshot());
	auto &result = *transaction;
	duckdb::lock_guard<duckdb::mutex> l(transaction_lock);
	transactions[result] = std::move(transaction);
	return result;
}

duckdb::ErrorData
PostgresTransactionManager::CommitTransaction(duckdb::ClientContext &context, duckdb::Transaction &transaction) {
	duckdb::lock_guard<duckdb::mutex> l(transaction_lock);
	ClosePostgresRelations(context);
	transactions.erase(transaction);
	return duckdb::ErrorData();
}

void
PostgresTransactionManager::RollbackTransaction(duckdb::Transaction &transaction) {
	duckdb::lock_guard<duckdb::mutex> l(transaction_lock);
	duckdb::shared_ptr<duckdb::ClientContext> context = transaction.context.lock();
	if (context) {
		ClosePostgresRelations(*context);
	}
	transactions.erase(transaction);
}

void
PostgresTransactionManager::Checkpoint(duckdb::ClientContext &, bool /*force*/) {
}

} // namespace pgduckdb
