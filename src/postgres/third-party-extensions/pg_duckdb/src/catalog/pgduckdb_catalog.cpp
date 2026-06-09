#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "pgduckdb/catalog/pgduckdb_catalog.hpp"
#include "pgduckdb/catalog/pgduckdb_schema.hpp"
#include "pgduckdb/catalog/pgduckdb_storage.hpp"
#include "pgduckdb/catalog/pgduckdb_transaction.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

PostgresCatalog::PostgresCatalog(duckdb::AttachedDatabase &_db, const duckdb::string &connection_string)
    : Catalog(_db), path(connection_string), schemas() {
}

duckdb::unique_ptr<duckdb::Catalog>
PostgresCatalog::Attach(duckdb::optional_ptr<duckdb::StorageExtensionInfo>, duckdb::ClientContext &,
                        duckdb::AttachedDatabase &db, const duckdb::string &, duckdb::AttachInfo &info,
                        duckdb::AttachOptions &) {
	return duckdb::make_uniq<PostgresCatalog>(db, info.path);
}

// ------------------ Catalog API ---------------------

void
PostgresCatalog::Initialize(bool /*load_builtin*/) {
}

duckdb::string
PostgresCatalog::GetCatalogType() {
	return "pgduckdb";
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresCatalog::CreateSchema(duckdb::CatalogTransaction, duckdb::CreateSchemaInfo &) {
	throw duckdb::NotImplementedException("CreateSchema not supported yet");
}

duckdb::optional_ptr<duckdb::SchemaCatalogEntry>
PostgresCatalog::LookupSchema(duckdb::CatalogTransaction catalog_transaction,
                              const duckdb::EntryLookupInfo &schema_lookup, duckdb::OnEntryNotFound) {
	auto &pg_transaction = catalog_transaction.transaction->Cast<PostgresTransaction>();
	const auto catalog_type = schema_lookup.GetCatalogType();
	if (catalog_type != duckdb::CatalogType::SCHEMA_ENTRY) {
		throw duckdb::NotImplementedException("LookupSchema only supports SCHEMA_ENTRY");
	}

	auto res = pg_transaction.GetCatalogEntry(catalog_type, schema_lookup.GetEntryName(), "");
	D_ASSERT(res);
	D_ASSERT(res->type == duckdb::CatalogType::SCHEMA_ENTRY);
	return (duckdb::SchemaCatalogEntry *)res.get();
}

void
PostgresCatalog::ScanSchemas(duckdb::ClientContext &, std::function<void(duckdb::SchemaCatalogEntry &)>) {
}

duckdb::PhysicalOperator &
PostgresCatalog::PlanCreateTableAs(duckdb::ClientContext &, duckdb::PhysicalPlanGenerator &,
                                   duckdb::LogicalCreateTable &, duckdb::PhysicalOperator &) {
	throw duckdb::NotImplementedException("PlanCreateTableAs not supported yet");
}

duckdb::PhysicalOperator &
PostgresCatalog::PlanInsert(duckdb::ClientContext &, duckdb::PhysicalPlanGenerator &, duckdb::LogicalInsert &,
                            duckdb::optional_ptr<duckdb::PhysicalOperator>) {
	throw duckdb::NotImplementedException("PlanInsert not supported yet");
}

duckdb::PhysicalOperator &
PostgresCatalog::PlanDelete(duckdb::ClientContext &, duckdb::PhysicalPlanGenerator &, duckdb::LogicalDelete &,
                            duckdb::PhysicalOperator &) {
	throw duckdb::NotImplementedException("PlanDelete not supported yet");
}

duckdb::PhysicalOperator &
PostgresCatalog::PlanUpdate(duckdb::ClientContext &, duckdb::PhysicalPlanGenerator &, duckdb::LogicalUpdate &,
                            duckdb::PhysicalOperator &) {
	throw duckdb::NotImplementedException("PlanUpdate not supported yet");
}

duckdb::unique_ptr<duckdb::LogicalOperator>
PostgresCatalog::BindCreateIndex(duckdb::Binder &, duckdb::CreateStatement &, duckdb::TableCatalogEntry &,
                                 duckdb::unique_ptr<duckdb::LogicalOperator>) {
	throw duckdb::NotImplementedException("BindCreateIndex not supported yet");
}

duckdb::DatabaseSize
PostgresCatalog::GetDatabaseSize(duckdb::ClientContext &) {
	throw duckdb::NotImplementedException("GetDatabaseSize not supported yet");
}

bool
PostgresCatalog::InMemory() {
	return false;
}

duckdb::string
PostgresCatalog::GetDBPath() {
	return path;
}

void
PostgresCatalog::DropSchema(duckdb::ClientContext &, duckdb::DropInfo &) {
	throw duckdb::NotImplementedException("DropSchema not supported yet");
}

} // namespace pgduckdb
