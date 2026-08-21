#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/storage/table_storage_info.hpp"

#include "pgduckdb/pg/declarations.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

class PostgresTable : public duckdb::TableCatalogEntry {
public:
	PostgresTable(duckdb::Catalog &catalog, duckdb::SchemaCatalogEntry &schema, duckdb::CreateTableInfo &info,
	              Relation rel, Cardinality cardinality, Snapshot snapshot);

	virtual ~PostgresTable();

	duckdb::unique_ptr<duckdb::BaseStatistics> GetStatistics(duckdb::ClientContext &context,
	                                                         duckdb::column_t column_id) override;
	duckdb::TableFunction GetScanFunction(duckdb::ClientContext &context,
	                                      duckdb::unique_ptr<duckdb::FunctionData> &bind_data) override;
	duckdb::TableStorageInfo GetStorageInfo(duckdb::ClientContext &context) override;

	static Relation OpenRelation(Oid relid);
	static void SetTableInfo(duckdb::CreateTableInfo &info, Relation rel);

protected:
	Relation rel;
	Cardinality cardinality;
	Snapshot snapshot;

private:
	PostgresTable(const PostgresTable &) = delete;
	PostgresTable &operator=(const PostgresTable &) = delete;
};

} // namespace pgduckdb
