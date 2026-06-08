#pragma once

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "pgduckdb/pg/declarations.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

class PostgresSchema : public duckdb::SchemaCatalogEntry {
public:
	PostgresSchema(duckdb::Catalog &catalog, duckdb::CreateSchemaInfo &info, Snapshot snapshot);

	// -- Schema API --
	void Scan(duckdb::ClientContext &context, duckdb::CatalogType type,
	          const std::function<void(CatalogEntry &)> &callback) override;
	void Scan(duckdb::CatalogType type, const std::function<void(duckdb::CatalogEntry &)> &callback) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateIndex(duckdb::CatalogTransaction transaction,
	                                                       duckdb::CreateIndexInfo &info,
	                                                       duckdb::TableCatalogEntry &table) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateFunction(duckdb::CatalogTransaction transaction,
	                                                          duckdb::CreateFunctionInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateTable(duckdb::CatalogTransaction transaction,
	                                                       duckdb::BoundCreateTableInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateView(duckdb::CatalogTransaction transaction,
	                                                      duckdb::CreateViewInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateSequence(duckdb::CatalogTransaction transaction,
	                                                          duckdb::CreateSequenceInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateTableFunction(duckdb::CatalogTransaction transaction,
	                                                               duckdb::CreateTableFunctionInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateCopyFunction(duckdb::CatalogTransaction transaction,
	                                                              duckdb::CreateCopyFunctionInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreatePragmaFunction(duckdb::CatalogTransaction transaction,
	                                                                duckdb::CreatePragmaFunctionInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateCollation(duckdb::CatalogTransaction transaction,
	                                                           duckdb::CreateCollationInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateType(duckdb::CatalogTransaction transaction,
	                                                      duckdb::CreateTypeInfo &info) override;
	duckdb::optional_ptr<duckdb::CatalogEntry> LookupEntry(duckdb::CatalogTransaction transaction,
	                                                       const duckdb::EntryLookupInfo &lookup_info) override;
	void DropEntry(duckdb::ClientContext &context, duckdb::DropInfo &info) override;
	void Alter(duckdb::CatalogTransaction transaction, duckdb::AlterInfo &info) override;

	Snapshot snapshot;
	duckdb::Catalog &catalog;

private:
	PostgresSchema(const PostgresSchema &) = delete;
	PostgresSchema &operator=(const PostgresSchema &) = delete;
};

} // namespace pgduckdb
