#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pgduckdb_extensions.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"
#include "pgduckdb/pg/functions.hpp"
#include "pgduckdb/pg/snapshots.hpp"
#include "pgduckdb/pg/relations.hpp"

extern "C" {
#include "postgres.h"

#include "access/table.h"
#include "access/genam.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/rel.h"
}

namespace pgduckdb {

namespace ddb {

std::string
LoadExtensionQuery(const std::string &extension_name) {
	return "LOAD " + duckdb::KeywordHelper::WriteQuoted(extension_name);
}

std::string
InstallExtensionQuery(const std::string &extension_name, const std::string &repository) {
	auto install_extension_command = "INSTALL " + duckdb::KeywordHelper::WriteQuoted(extension_name) + " FROM ";

	if (repository == "core" || repository == "core_nightly" || repository == "community" ||
	    repository == "local_build_debug" || repository == "local_build_release") {
		install_extension_command += repository;
	} else {
		install_extension_command += duckdb::KeywordHelper::WriteQuoted(repository);
	}

	return install_extension_command;
}

} // namespace ddb

std::vector<DuckdbExtension>
ReadDuckdbExtensions() {
	HeapTuple tuple = NULL;
	Oid duckdb_extension_table_relation_id = GetRelidFromSchemaAndTable("duckdb", "extensions");
	Relation duckdb_extension_relation = table_open(duckdb_extension_table_relation_id, AccessShareLock);
	SysScanDescData *scan =
	    systable_beginscan(duckdb_extension_relation, InvalidOid, false, GetActiveSnapshot(), 0, NULL);
	std::vector<DuckdbExtension> duckdb_extensions;

	while (HeapTupleIsValid(tuple = systable_getnext(scan))) {
		Datum datum_array[Natts_duckdb_extension];
		bool is_null_array[Natts_duckdb_extension];

		heap_deform_tuple(tuple, RelationGetDescr(duckdb_extension_relation), datum_array, is_null_array);
		DuckdbExtension extension;

		extension.name = pg::DatumToString(datum_array[Anum_duckdb_extension_name - 1]);
		extension.autoload = DatumGetBool(datum_array[Anum_duckdb_extension_autoload - 1]);
		extension.repository = pg::DatumToString(datum_array[Anum_duckdb_extension_repository - 1]);
		duckdb_extensions.push_back(extension);
	}

	systable_endscan(scan);
	table_close(duckdb_extension_relation, NoLock);
	return duckdb_extensions;
}

} // namespace pgduckdb

extern "C" {

DECLARE_PG_FUNCTION(install_extension) {
	std::string extension_name = pgduckdb::pg::GetArgString(fcinfo, 0);
	std::string repository = pgduckdb::pg::GetArgString(fcinfo, 1);

	pgduckdb::DuckDBQueryOrThrow(pgduckdb::ddb::InstallExtensionQuery(extension_name, repository));

	Datum extension_name_datum = CStringGetTextDatum(extension_name.c_str());
	Datum repository_datum = CStringGetTextDatum(repository.c_str());

	Oid arg_types[] = {TEXTOID, TEXTOID};
	Datum values[] = {extension_name_datum, repository_datum};

	SPI_connect();
	auto ret = SPI_execute_with_args(R"(
		INSERT INTO duckdb.extensions (name, autoload, repository)
		VALUES ($1, true, $2)
		ON CONFLICT (name) DO UPDATE SET autoload = true
		)",
	                                 lengthof(arg_types), arg_types, values, NULL, false, 0);
	if (ret != SPI_OK_INSERT) {
		throw duckdb::InternalException("SPI_exec failed: error code %s", SPI_result_code_string(ret));
	}

	SPI_finish();

	PG_RETURN_VOID();
}

DECLARE_PG_FUNCTION(duckdb_load_extension) {
	std::string extension_name = pgduckdb::pg::GetArgString(fcinfo, 0);

	pgduckdb::DuckDBQueryOrThrow(pgduckdb::ddb::LoadExtensionQuery(extension_name));

	PG_RETURN_VOID();
}

DECLARE_PG_FUNCTION(duckdb_autoload_extension) {
	std::string extension_name = pgduckdb::pg::GetArgString(fcinfo, 0);
	bool autoload_datum = pgduckdb::pg::GetArgDatum(fcinfo, 1);

	Datum extension_name_datum = CStringGetTextDatum(extension_name.c_str());

	Oid arg_types[] = {TEXTOID, BOOLOID};
	Datum values[] = {extension_name_datum, autoload_datum};

	SPI_connect();
	auto ret = SPI_execute_with_args(R"(
		UPDATE duckdb.extensions SET autoload = $2 WHERE name = $1
		)",
	                                 lengthof(arg_types), arg_types, values, NULL, false, 0);
	if (ret != SPI_OK_UPDATE) {
		throw duckdb::InternalException("SPI_exec failed: error code %s", SPI_result_code_string(ret));
	}

	if (SPI_processed == 0) {
		throw duckdb::InvalidInputException("Extension '%s' does not exist", extension_name);
	}

	SPI_finish();

	PG_RETURN_VOID();
}
}
