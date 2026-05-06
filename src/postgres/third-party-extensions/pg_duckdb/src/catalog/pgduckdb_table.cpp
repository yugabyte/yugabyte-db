#include "pgduckdb/catalog/pgduckdb_table.hpp"

#include "pgduckdb/scan/postgres_scan.hpp"
#include "pgduckdb/catalog/pgduckdb_schema.hpp"
#include "pgduckdb/logger.hpp"
#include "pgduckdb/pg/relations.hpp"
#include "pgduckdb/pgduckdb_process_lock.hpp"
#include "pgduckdb/pgduckdb_types.hpp" // ConvertPostgresToDuckColumnType

#include "duckdb/parser/parsed_data/create_table_info.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

PostgresTable::PostgresTable(duckdb::Catalog &_catalog, duckdb::SchemaCatalogEntry &_schema,
                             duckdb::CreateTableInfo &_info, Relation _rel, Cardinality _cardinality,
                             Snapshot _snapshot)
    : duckdb::TableCatalogEntry(_catalog, _schema, _info), rel(_rel), cardinality(_cardinality), snapshot(_snapshot) {
}

PostgresTable::~PostgresTable() {
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	CloseRelation(rel);
}

Relation
PostgresTable::OpenRelation(Oid relid) {
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	return pgduckdb::OpenRelation(relid);
}

void
PostgresTable::SetTableInfo(duckdb::CreateTableInfo &info, Relation rel) {
	auto tupleDesc = RelationGetDescr(rel);

	const auto n = GetTupleDescNatts(tupleDesc);
	for (int i = 0; i < n; ++i) {
		Form_pg_attribute attr = GetAttr(tupleDesc, i);
		auto col_name = duckdb::string(GetAttName(attr));
		auto duck_type = ConvertPostgresToDuckColumnType(attr);
		info.columns.AddColumn(duckdb::ColumnDefinition(col_name, duck_type));
		/* Log column name and type */
		pd_log(DEBUG2, "(DuckDB/SetTableInfo) Column name: '%s', Type: %s", col_name.c_str(),
		       duck_type.ToString().c_str());
	}
}

duckdb::unique_ptr<duckdb::BaseStatistics>
PostgresTable::GetStatistics(duckdb::ClientContext &, duckdb::column_t) {
	throw duckdb::NotImplementedException("GetStatistics not supported yet");
}

duckdb::TableFunction
PostgresTable::GetScanFunction(duckdb::ClientContext &, duckdb::unique_ptr<duckdb::FunctionData> &bind_data) {
	bind_data = duckdb::make_uniq<PostgresScanFunctionData>(rel, cardinality, snapshot);
	return PostgresScanTableFunction();
}

duckdb::TableStorageInfo
PostgresTable::GetStorageInfo(duckdb::ClientContext &) {
	throw duckdb::NotImplementedException("GetStorageInfo not supported yet");
}

} // namespace pgduckdb
