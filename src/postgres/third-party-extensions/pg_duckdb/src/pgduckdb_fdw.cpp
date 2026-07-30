extern "C" {
#include "postgres.h"

#include "access/htup_details.h"
#include "access/reloptions.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_user_mapping.h"
#include "commands/defrem.h"
#include "catalog/dependency.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "optimizer/pathnode.h"
#include "utils/acl.h"
#include "utils/builtins.h"

#include "pgduckdb/vendor/pg_list.hpp"
}

#include "pgduckdb/pg/string_utils.hpp"
#include "pgduckdb/pgduckdb_background_worker.hpp"
#include "pgduckdb/pgduckdb_fdw.hpp"
#include "pgduckdb/pgduckdb_secrets_helper.hpp"
#include "pgduckdb/pgduckdb_userdata_cache.hpp"

extern "C" {

typedef enum { ANY, MD, S3, GCS, SENTINEL } FdwType;

struct PGDuckDBFdwOption {
	const char *optname;
	Oid context; /* Oid of catalog in which option may appear */
	bool required = false;
};

/*
 * Valid options for a pgduckdb_fdw that has a `motherduck` type.
 */
static const struct PGDuckDBFdwOption valid_md_options[] = {
    /* --- MD Server --- */
    {"tables_owner_role",
     ForeignServerRelationId}, // the role bgw assigns to MD tables (by default server creator/owner)
    {"background_catalog_refresh_inactivity_timeout", ForeignServerRelationId},
    {"default_database", ForeignServerRelationId}, // Name of the MotherDuck database to be synced (default "my_db")

    /* --- MD Mapping --- */
    {"token", UserMappingRelationId, true}, // token to authenticate with MotherDuck

    /* Sentinel */
    {NULL, SENTINEL, InvalidOid}};

PG_FUNCTION_INFO_V1(pgduckdb_fdw_handler);
Datum
pgduckdb_fdw_handler(PG_FUNCTION_ARGS __attribute__((unused))) {
	PG_RETURN_POINTER(nullptr);
}

bool
IsValidMdOption(const char *optname, Oid context) {
	for (const struct PGDuckDBFdwOption *opt = valid_md_options; opt->optname != NULL; ++opt) {
		if (opt->context == context && strcmp(optname, opt->optname) == 0) {
			return true;
		}
	}
	return false;
}

void
ValidateHasRequiredMdOptions(List *options_list, Oid context) {
	for (const struct PGDuckDBFdwOption *opt = valid_md_options; opt->optname != NULL; ++opt) {
		if (!opt->required || opt->context != context) {
			continue;
		}

		bool found = false;
		foreach_node(DefElem, def, options_list) {
			if (strcmp(def->defname, opt->optname) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			elog(ERROR, "Missing required option: '%s'", opt->optname);
		}
	}
}

namespace pgduckdb {
namespace pg {

namespace {
Oid
ExtractOidFromSPIQuery(int ret) {
	if (ret != SPI_OK_SELECT) {
		elog(ERROR, "SPI_exec failed: error code %s", SPI_result_code_string(ret));
	}

	if (SPI_processed == 0) {
		return InvalidOid;
	}

	HeapTuple tuple = SPI_tuptable->vals[0];
	bool isnull = false;
	Datum oid_datum = SPI_getbinval(tuple, SPI_tuptable->tupdesc, 1, &isnull);

	if (isnull) {
		elog(ERROR, "FATAL: Expected oid to be returned, but found NULL");
	}

	return DatumGetObjectId(oid_datum);
}

Oid
FindOid(const char *query) {
	SPI_connect();
	int ret = SPI_exec(query, 0);
	auto oid = ExtractOidFromSPIQuery(ret);
	SPI_finish();
	return oid;
}

} // namespace

Oid
FindMotherDuckForeignServerOid() {
	return FindOid(R"(
		SELECT fs.oid
		FROM pg_foreign_server fs
		INNER JOIN pg_foreign_data_wrapper fdw ON fdw.oid = fs.srvfdw
		WHERE fdw.fdwname = 'duckdb' AND fs.srvtype = 'motherduck';
	)");
}

// Return the `default_database` setting defined in the `motherduck` SERVER
// if it exists, returns nullptr otherwise.
const char *
FindMotherDuckDefaultDatabase() {
	Oid server_oid = GetMotherduckForeignServerOid();
	if (server_oid == InvalidOid) {
		return nullptr;
	}

	auto server = GetForeignServer(server_oid);
	return FindOption(server->options, "default_database");
}

const char *
FindMotherDuckBackgroundCatalogRefreshInactivityTimeout() {
	Oid server_oid = GetMotherduckForeignServerOid();
	if (server_oid == InvalidOid) {
		return nullptr;
	}

	auto server = GetForeignServer(server_oid);
	return FindOption(server->options, "background_catalog_refresh_inactivity_timeout");
}

// * if `pgduckdb::is_background_worker` then:
//   > returns `token` option defined in the USER MAPPING of the SERVER's owner if it exists
//   > returns nullptr otherwise
//
// * otherwise:
//   > returns `token` option defined in the USER MAPPING of the current user if it exists
//   > returns nullptr otherwise
const char *
FindMotherDuckToken() {
	Oid server_oid = GetMotherduckForeignServerOid();
	if (server_oid == InvalidOid) {
		return nullptr;
	}

	auto userid = GetUserId();
	if (pgduckdb::is_background_worker) {
		auto server = GetForeignServer(server_oid);
		auto user_mapping = FindUserMapping(server->owner, server_oid, true);
		if (user_mapping == nullptr) {
			elog(WARNING, "No USER MAPPING found for owner of server %s", server->servername);
			return nullptr;
		} else if (user_mapping->options == nullptr) {
			elog(WARNING, "No token found in owner's USER MAPPING of server %s", server->servername);
			return nullptr;
		}

		// SERVER's owner has an UM, get token from its options:
		return FindOption(user_mapping->options, "token");
	}

	// Not a background worker, get token from current user's UM:
	if (GetMotherDuckUserMappingOid() == InvalidOid) {
		return nullptr;
	}

	auto user_mapping = FindUserMapping(userid, server_oid, true);
	return user_mapping && user_mapping->options ? FindOption(user_mapping->options, "token") : nullptr;
}

Oid
FindUserMappingOid(Oid user_oid, Oid server_oid) {
	auto user_mapping = FindUserMapping(user_oid, server_oid);
	return user_mapping ? user_mapping->umid : InvalidOid;
}

Oid
GetMotherDuckPostgresRoleOid(Oid server_oid) {
	auto server = GetForeignServer(server_oid);
	auto role = FindOption(server->options, "tables_owner_role");
	return role == nullptr ? server->owner : get_role_oid(role, true);
}

void
ValidateHasNoMotherduckForeignServer() {
	auto oid = FindMotherDuckForeignServerOid();
	if (oid != InvalidOid) {
		elog(ERROR, "MotherDuck FDW already exists ('%s')", GetForeignServer(oid)->servername);
	}
}

void
ValidateMdOptions(List *options_list, Oid context) {
	// Make sure all options are valid
	foreach_node(DefElem, def, options_list) {
		if (!IsValidMdOption(def->defname, context)) {
			elog(ERROR, "Unknown option: '%s'", def->defname);
		}
	}

	// Make sure we have all required options
	ValidateHasRequiredMdOptions(options_list, context);
}

Datum
ValidateMotherduckServerFdw(List *options_list, Oid context) {
	// For now only accept one MotherDuck FDW globally
	// can be relaxed eventually with https://github.com/duckdb/pg_duckdb/pull/545

	// TODO: take a global lock to make this check
	ValidateHasNoMotherduckForeignServer();

	ValidateMdOptions(options_list, context);

	// Validate tables_owner_role
	// TODO - can we add a "link" to the role here? (so there's an error when dropping the role)
	auto tables_owner_role = FindOption(options_list, "tables_owner_role");
	if (tables_owner_role != nullptr) {
		auto role_oid = get_role_oid(tables_owner_role, true);
		if (role_oid == InvalidOid) {
			elog(ERROR, "Role '%s' does not exist", tables_owner_role);
		}
	}

	PG_RETURN_VOID();
}

} // namespace pg

void
RecordDependencyOnMDServer(ObjectAddress *object_address) {
	ObjectAddress server_address = {
	    .classId = ForeignServerRelationId,
	    .objectId = GetMotherduckForeignServerOid(),
	    .objectSubId = 0,
	};
	recordDependencyOn(object_address, &server_address, DEPENDENCY_NORMAL);
}

const char *CurrentServerType = nullptr;
Oid CurrentServerOid = InvalidOid;

} // namespace pgduckdb

PG_FUNCTION_INFO_V1(pgduckdb_fdw_validator);
Datum
pgduckdb_fdw_validator(PG_FUNCTION_ARGS) {
	using namespace pgduckdb::pg;
	Oid catalog = PG_GETARG_OID(1);

	if (catalog == ForeignTableRelationId || catalog == ForeignTableRelidIndexId) {
		elog(ERROR, "pgduckdb FDW only support 'SERVER' and 'USER MAPPING' objects.");
	}

	List *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
	if (catalog == ForeignDataWrapperRelationId) {
		// `CREATE FOREIGN DATA WRAPPER` statement
		foreach_node(DefElem, def, options_list) {
			elog(ERROR, "'duckdb' FDW does not take any option, found '%s'", def->defname);
		}

		PG_RETURN_VOID(); // no additional validation needed
	} else if (catalog == ForeignServerRelationId) {
		auto server_type = pgduckdb::CurrentServerType;
		pgduckdb::CurrentServerType = nullptr;
		if (AreStringEqual(server_type, "motherduck")) {
			ValidateMotherduckServerFdw(options_list, catalog);
		} else {
			ValidateDuckDBSecret(server_type, options_list);
		}

		PG_RETURN_VOID();
	} else if (catalog == UserMappingRelationId) {
		auto server = GetForeignServer(pgduckdb::CurrentServerOid);
		pgduckdb::CurrentServerOid = InvalidOid;
		if (AreStringEqual(server->servertype, "motherduck")) {
			ValidateMdOptions(options_list, catalog);
		} else {
			ValidateDuckDBSecret(server->servertype, server->options, options_list);
		}

		// PG only accepts at most one USER MAPPING for a given (user, server)
		// So no need to check for duplicates.

		PG_RETURN_VOID();
	}

	elog(ERROR, "Unknown catalog: %d", catalog);

	PG_RETURN_VOID();
}

} // extern "C"
