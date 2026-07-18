/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/constant.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};

use bson::{rawdoc, RawDocumentBuf};

use crate::{
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    protocol::{self, OK_SUCCEEDED},
    responses::{RawResponse, Response},
};

pub fn ok_response() -> Response {
    Response::Raw(RawResponse(rawdoc! {
        "ok": OK_SUCCEEDED
    }))
}

pub async fn process_build_info(
    dynamic_config: &Arc<dyn DynamicConfiguration>,
) -> Result<Response> {
    let version = dynamic_config.server_version().await;
    Ok(Response::Raw(RawResponse(rawdoc! {
        "version": version.as_str(),
        "versionArray": version.as_bson_array(),
        "bits": 64,
        "maxBsonObjectSize": protocol::MAX_BSON_OBJECT_SIZE,
        "ok":OK_SUCCEEDED,
    })))
}

pub fn process_get_cmd_line_opts() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "argv": [],
        "ok":OK_SUCCEEDED,
    })))
}

pub fn process_is_db_grid(context: &ConnectionContext) -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "isdbgrid":1.0,
        "hostname":context.service_context.setup_configuration().node_host_name(),
        "ok":OK_SUCCEEDED,
    })))
}

pub fn process_get_rw_concern(request_context: &mut RequestContext<'_>) -> Result<Response> {
    let request = request_context.payload;
    let request_info = request_context.info;

    request.extract_fields(|k, _| match k {
        "getDefaultRWConcern" | "inMemory" | "comment" | "lsid" | "$db" => Ok(()),
        other => Err(DocumentDBError::documentdb_error(
            ErrorCode::UnknownBsonField,
            format!("Not a valid value for getDefaultRWConcern: {other}"),
        )),
    })?;

    if request_info.db()? != "admin" {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::Unauthorized,
            "Only the admin database can process getDefaultRWConcern.".to_string(),
        ));
    }

    Ok(Response::Raw(RawResponse(rawdoc! {
        "defaultReadConcern": {
            "level":"majority",
        },
        "defaultWriteConcern": {
            "w": "majority",
            "wtimeout": 0,
        },
        "defaultReadConcernSource": "implicit",
        "defaultWriteConcernSource": "implicit",
        "ok":OK_SUCCEEDED,
    })))
}

pub fn process_get_log() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "log":[],
        "totalLinesWritten":0,
        "ok":OK_SUCCEEDED,
    })))
}

pub fn process_connection_status() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "authInfo": {
            "authenticatedUsers": [],
            "authenticatedUserRoles": [],
            "authenticatedUserPrivileges": [],
        },
        "ok":OK_SUCCEEDED,
    })))
}

fn local_time() -> Result<u32> {
    u32::try_from(
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| {
                DocumentDBError::internal_error("Failed to get the current time".to_string())
            })?
            .as_secs(),
    )
    .map_err(|_| DocumentDBError::internal_error("Current time exceeded an u32".to_string()))
}

pub fn process_host_info() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "system": {
            "currentTime": bson::Timestamp{ time: local_time()?, increment: 0},
            "memSizeMB": 0,
        },
        "os": {
            "name":"",
            "type":"",
        },
        "extra": {
            "cpuFrequencyMHz": 0,
        },
        "ok": OK_SUCCEEDED,
    })))
}

pub fn process_prepare_transaction() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "prepareTimestamp":  bson::Timestamp{ time: local_time()?, increment: 0 },
        "ok": OK_SUCCEEDED,
    })))
}

pub fn process_whats_my_uri() -> Result<Response> {
    Ok(Response::Raw(RawResponse(rawdoc! {
        "ok": OK_SUCCEEDED,
    })))
}

struct CommandInfo {
    command_name: &'static str,
    admin_only: bool,
    help: &'static str,
    secondary_ok: bool,
    requires_auth: bool,
    secondary_override_ok: Option<bool>,
}

static SUPPORTED_COMMANDS : [CommandInfo; 57] = [
	CommandInfo {
		command_name: "abortTransaction",
		admin_only: true,
		help: "Takes a transaction that's active and aborts it.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "aggregate",
		admin_only: false,
		help: "Performs aggregation on the data, such as filtering, grouping, and sorting, and returns computed results. For more details, refer to https://aka.ms/AAxl8do.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "authenticate",
		admin_only: false,
		help: "Authenticates the underlying connection using user-supplied credentials.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "buildInfo",
		admin_only: false,
		help: "Returns the version information for the cluster.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "collMod",
		admin_only: false,
		help: "Configure options for a collection.\ne.g. { collMod: 'name', index: {keyPattern: {key: 1}, expireAfterSeconds: 10}, dryRun: false }\n     { collMod: 'name', index: {name: 'indexName', expireAfterSeconds: 120} }\n",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "collStats",
		admin_only: false,
		help: "Get statistics about a collection, returns the average size in bytes.\ne.g. { collStats : \"shelter.dogs\" , scale : 1048576 } (returns result in Mb)",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "commitTransaction",
		admin_only: true,
		help: "Finish a running transaction.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "connectionStatus",
		admin_only: false,
		help: "Get information about a connection like the roles of logged in users.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "count",
		admin_only: false,
		help: "Get the number of documents in a collection. For more details, refer to https://aka.ms/AAxl0ve.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "create",
		admin_only: false,
		help: "Create a new collection (or view).",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "createIndex",
		admin_only: false,
		help: "Create an index on a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "createIndexes",
		admin_only: false,
		help: "Create multiple indexes on a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "currentOp",
		admin_only: true,
		help: "Get information about currently running operations.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "dbStats",
		admin_only: false,
		help: "Get statistics about a database, returns the average size in bytes.\ne.g. { dbStats : 1 , scale : 1048576 } (returns result in Mb).",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "delete",
		admin_only: false,
		help: "Remove documents from a collection. For more details, refer to https://aka.ms/AAxl8en.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "distinct",
		admin_only: false,
		help: "Get the unique values for a field in a collection. For more details, refer to https://aka.ms/AAxl0vh.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "drop",
		admin_only: false,
		help: "Remove a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "dropDatabase",
		admin_only: false,
		help: "Remove an entire database.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "dropIndexes",
		admin_only: false,
		help: "Remove the indexes from a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "enableSharding",
		admin_only: true,
		help: "Marks the database as shard-enabled, allowing sharded collections to be created.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "endSessions",
		admin_only: false,
		help: "Stop multiple sessions and their operations.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "explain",
		admin_only: false,
		help: "Get information about an operation.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "find",
		admin_only: false,
		help: "Search for documents in a collection. For more details, refer to https://aka.ms/AAxlf5o.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "findAndModify",
		admin_only: false,
		help: "Update the fields of a single document that matches a query. For more details, refer to https://aka.ms/AAxl0vr.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getCmdLineOpts",
		admin_only: true,
		help: "Get the command line options used to start the server.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getDefaultRWConcern",
		admin_only: true,
		help: "Get the Read/Write concern for the cluster.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getLastError",
		admin_only: false,
		help: "Get the error information for the most recent operation run.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getLog",
		admin_only: true,
		help: "Get recent log entries.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getMore",
		admin_only: false,
		help: "Get the next page of documents from a cursor. For more details, refer to https://aka.ms/AAxl8es.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getParameter",
		admin_only: true,
		help: "Get the value of a particular parameter.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "getnonce",
		admin_only: false,
		help: "unused",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "hello",
		admin_only: false,
		help: "Gets information about the cluster topology.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "hostInfo",
		admin_only: false,
		help: "Get details about the host machine.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "insert",
		admin_only: false,
		help: "The insert command can be used to add one or more documents to a collection. For more details, refer to https://aka.ms/AAxkukq.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "isMaster",
		admin_only: false,
		help: "Gets information about the cluster topology.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "isdbgrid",
		admin_only: false,
		help: "Check if the instance is sharded.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "killCursors",
		admin_only: false,
		help: "Stop a set of cursors.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "killOp",
		admin_only: true,
		help: "Stop a running operation.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "killSessions",
		admin_only: false,
		help: "Stop a session along with its operations.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "listCollections",
		admin_only: false,
		help: "Show all collections in a particular database.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "listCommands",
		admin_only: false,
		help: "Show all possible commands.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "listDatabases",
		admin_only: true,
		help: "Show all databases on the cluster.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "listIndexes",
		admin_only: false,
		help: "Show all indexes on a particular collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: Some(false),
	},
	CommandInfo {
		command_name: "logout",
		admin_only: false,
		help: "Log out of the current session.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "ping",
		admin_only: false,
		help: "Check if the server is able to respond to network requests.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "reIndex",
		admin_only: false,
		help: "Rebuild an index.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "renameCollection",
		admin_only: true,
		help: "Change the name of a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "reshardCollection",
		admin_only: true,
		help: "Change a sharded collection's shard key.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "saslContinue",
		admin_only: false,
		help: "Perform the next steps of a SASL authentication.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "saslStart",
		admin_only: false,
		help: "Initiate a SASL authentication.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "serverStatus",
		admin_only: false,
		help: "Get administrative details about the server.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "shardCollection",
		admin_only: true,
		help: "Make a collection sharded using a given key.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "startSession",
		admin_only: false,
		help: "Initiate a logical session for isolating operations.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "unshardCollection",
		admin_only: true,
		help: "Remove sharding from a collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "update",
		admin_only: false,
		help: "The update command can be used to update one or multiple documents based on filtering criteria. Values of fields can be changed, new fields and values can be added and existing fields can be removed. For more details, refer to https://aka.ms/AAxjzfd.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "validate",
		admin_only: false,
		help: "Check for correctness on a particular collection.",
		secondary_ok: false,
		requires_auth: true,
		secondary_override_ok: None,
	},
	CommandInfo {
		command_name: "whatsmyuri",
		admin_only: false,
		help: "Get the URI of the current connection.",
		secondary_ok: false,
		requires_auth: false,
		secondary_override_ok: None,
	}
];

pub fn list_commands() -> Result<Response> {
    let mut commands_doc = RawDocumentBuf::new();
    for command in &SUPPORTED_COMMANDS {
        let mut doc = rawdoc! {
            "adminOnly": command.admin_only,
            "apiVersions": [],
            "deprecatedApiVersions": [],
            "help": command.help,
            "secondaryOk": command.secondary_ok,
            "requiresAuth": command.requires_auth,
        };
        if let Some(secondary_override) = command.secondary_override_ok {
            doc.append("secondaryOverrideOk", secondary_override)
        }
        commands_doc.append(command.command_name, doc);
    }

    Ok(Response::Raw(RawResponse(rawdoc! {
        "commands": commands_doc,
        "ok": OK_SUCCEEDED,
    })))
}
