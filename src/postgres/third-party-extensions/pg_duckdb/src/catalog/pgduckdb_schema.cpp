#include "pgduckdb/catalog/pgduckdb_schema.hpp"
#include "pgduckdb/catalog/pgduckdb_table.hpp"
#include "pgduckdb/catalog/pgduckdb_transaction.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

PostgresSchema::PostgresSchema(duckdb::Catalog &_catalog, duckdb::CreateSchemaInfo &_info, Snapshot _snapshot)
    : SchemaCatalogEntry(_catalog, _info), snapshot(_snapshot), catalog(_catalog) {
}

void
PostgresSchema::Scan(duckdb::ClientContext &, duckdb::CatalogType, const std::function<void(CatalogEntry &)> &) {
}

void
PostgresSchema::Scan(duckdb::CatalogType, const std::function<void(duckdb::CatalogEntry &)> &) {
	throw duckdb::NotImplementedException("Scan(no context) not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateIndex(duckdb::CatalogTransaction, duckdb::CreateIndexInfo &, duckdb::TableCatalogEntry &) {
	throw duckdb::NotImplementedException("CreateIndex not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateFunction(duckdb::CatalogTransaction, duckdb::CreateFunctionInfo &) {
	throw duckdb::NotImplementedException("CreateFunction not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateTable(duckdb::CatalogTransaction, duckdb::BoundCreateTableInfo &) {
	throw duckdb::NotImplementedException("CreateTable not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateView(duckdb::CatalogTransaction, duckdb::CreateViewInfo &) {
	throw duckdb::NotImplementedException("CreateView not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateSequence(duckdb::CatalogTransaction, duckdb::CreateSequenceInfo &) {
	throw duckdb::NotImplementedException("CreateSequence not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateTableFunction(duckdb::CatalogTransaction, duckdb::CreateTableFunctionInfo &) {
	throw duckdb::NotImplementedException("CreateTableFunction not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateCopyFunction(duckdb::CatalogTransaction, duckdb::CreateCopyFunctionInfo &) {
	throw duckdb::NotImplementedException("CreateCopyFunction not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreatePragmaFunction(duckdb::CatalogTransaction, duckdb::CreatePragmaFunctionInfo &) {
	throw duckdb::NotImplementedException("CreatePragmaFunction not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateCollation(duckdb::CatalogTransaction, duckdb::CreateCollationInfo &) {
	throw duckdb::NotImplementedException("CreateCollation not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::CreateType(duckdb::CatalogTransaction, duckdb::CreateTypeInfo &) {
	throw duckdb::NotImplementedException("CreateType not supported yet");
}

duckdb::optional_ptr<duckdb::CatalogEntry>
PostgresSchema::LookupEntry(duckdb::CatalogTransaction _catalog_transaction,
                            const duckdb::EntryLookupInfo &lookup_info) {
	auto &pg_transaction = _catalog_transaction.transaction->Cast<PostgresTransaction>();
	return pg_transaction.GetCatalogEntry(lookup_info.GetCatalogType(), name, lookup_info.GetEntryName());
}

void
PostgresSchema::DropEntry(duckdb::ClientContext &, duckdb::DropInfo &) {
	throw duckdb::NotImplementedException("DropEntry not supported yet");
}

void
PostgresSchema::Alter(duckdb::CatalogTransaction, duckdb::AlterInfo &) {
	throw duckdb::NotImplementedException("Alter not supported yet");
}

} // namespace pgduckdb
