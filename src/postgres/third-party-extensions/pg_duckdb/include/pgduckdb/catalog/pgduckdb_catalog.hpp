#pragma once

#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/catalog/catalog.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

class PostgresSchema;

class PostgresCatalog : public duckdb::Catalog {
public:
	PostgresCatalog(duckdb::AttachedDatabase &db, const duckdb::string &connection_string);

	static duckdb::unique_ptr<duckdb::Catalog> Attach(duckdb::optional_ptr<duckdb::StorageExtensionInfo>,
	                                                  duckdb::ClientContext &, duckdb::AttachedDatabase &db,
	                                                  const duckdb::string &, duckdb::AttachInfo &info,
	                                                  duckdb::AttachOptions &options);

	// -- Catalog API --
	void Initialize(bool load_builtin) override;
	duckdb::string GetCatalogType() override;
	duckdb::optional_ptr<duckdb::CatalogEntry> CreateSchema(duckdb::CatalogTransaction transaction,
	                                                        duckdb::CreateSchemaInfo &info) override;
	duckdb::optional_ptr<duckdb::SchemaCatalogEntry> LookupSchema(duckdb::CatalogTransaction transaction,
	                                                              const duckdb::EntryLookupInfo &schema_lookup,
	                                                              const duckdb::OnEntryNotFound if_not_found) override;
	void ScanSchemas(duckdb::ClientContext &context,
	                 std::function<void(duckdb::SchemaCatalogEntry &)> callback) override;
	duckdb::PhysicalOperator &PlanCreateTableAs(duckdb::ClientContext &context, duckdb::PhysicalPlanGenerator &planner,
	                                            duckdb::LogicalCreateTable &op,
	                                            duckdb::PhysicalOperator &plan) override;
	duckdb::PhysicalOperator &PlanInsert(duckdb::ClientContext &context, duckdb::PhysicalPlanGenerator &planner,
	                                     duckdb::LogicalInsert &op,
	                                     duckdb::optional_ptr<duckdb::PhysicalOperator> plan) override;
	duckdb::PhysicalOperator &PlanDelete(duckdb::ClientContext &context, duckdb::PhysicalPlanGenerator &planner,
	                                     duckdb::LogicalDelete &op, duckdb::PhysicalOperator &plan) override;
	duckdb::PhysicalOperator &PlanUpdate(duckdb::ClientContext &context, duckdb::PhysicalPlanGenerator &planner,
	                                     duckdb::LogicalUpdate &op, duckdb::PhysicalOperator &plan) override;
	duckdb::unique_ptr<duckdb::LogicalOperator>
	BindCreateIndex(duckdb::Binder &binder, duckdb::CreateStatement &stmt, duckdb::TableCatalogEntry &table,
	                duckdb::unique_ptr<duckdb::LogicalOperator> plan) override;
	duckdb::DatabaseSize GetDatabaseSize(duckdb::ClientContext &context) override;
	bool InMemory() override;
	duckdb::string GetDBPath() override;
	void DropSchema(duckdb::ClientContext &context, duckdb::DropInfo &info) override;

	duckdb::string path;

private:
	duckdb::case_insensitive_map_t<duckdb::unique_ptr<PostgresSchema>> schemas;
};

} // namespace pgduckdb
