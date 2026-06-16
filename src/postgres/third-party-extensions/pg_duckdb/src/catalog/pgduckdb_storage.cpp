#include "pgduckdb/catalog/pgduckdb_storage.hpp"
#include "pgduckdb/catalog/pgduckdb_catalog.hpp"
#include "pgduckdb/catalog/pgduckdb_transaction_manager.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

static duckdb::unique_ptr<duckdb::TransactionManager>
CreateTransactionManager(duckdb::optional_ptr<duckdb::StorageExtensionInfo>, duckdb::AttachedDatabase &db,
                         duckdb::Catalog &catalog) {
	return duckdb::make_uniq<PostgresTransactionManager>(db, catalog.Cast<PostgresCatalog>());
}

PostgresStorageExtension::PostgresStorageExtension() {
	attach = PostgresCatalog::Attach;
	create_transaction_manager = CreateTransactionManager;
}

} // namespace pgduckdb
