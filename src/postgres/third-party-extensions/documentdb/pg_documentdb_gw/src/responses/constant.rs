/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/constant.rs
 *
 *-------------------------------------------------------------------------
 */

use std::fmt::Display;

use bson::ser::Error;

pub fn bson_serialize_error_message(error: Error) -> String {
    format!("Error serializing CommandError: {error}.")
}

pub fn value_access_error_message() -> String {
    "Value Access Error.".to_string()
}

pub fn documentdb_error_message() -> String {
    "DocumentDB error.".to_string()
}

pub fn pg_returned_invalid_response_message<E: Display>(error: E) -> String {
    format!("PG returned invalid response: {error}.")
}

pub fn duplicate_key_violation_message() -> &'static str {
    "Duplicate key violation on the requested collection."
}
