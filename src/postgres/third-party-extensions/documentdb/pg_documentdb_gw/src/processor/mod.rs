/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/mod.rs
 *
 *-------------------------------------------------------------------------
 */

mod constant;
mod cursor;
mod data_description;
mod data_management;
mod indexing;
mod ismaster;
mod process;
mod roles;
mod session;
mod transaction;
mod users;

pub use process::process_request;
