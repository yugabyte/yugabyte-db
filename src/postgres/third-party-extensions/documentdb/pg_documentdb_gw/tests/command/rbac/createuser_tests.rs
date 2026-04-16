/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/command/rbac/createuser_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{doc, Bson, Document};
use uuid::Uuid;

pub mod common;
pub use crate::common::rbac_utils::{user_exists, validate_user};

#[tokio::test]
async fn test_create_user() -> Result<(), mongodb::error::Error> {
    let client = common::initialize().await;
    let db_name = "admin";
    let db = client.database(db_name);
    let username = format!("user_{}", Uuid::new_v4().to_string().replace("-", ""));
    let user_id = format!("{}.{}", db_name, username);
    let role = "readAnyDatabase";

    db.run_command(doc! {
        "createUser": &username,
        "pwd": "Valid$1Pass",
        "roles": [ { "role": role, "db": db_name } ]
    })
    .await?;

    let users = db
        .run_command(doc! {
            "usersInfo": &username
        })
        .await?;

    validate_user(&users, &user_id, &username, db_name, role);

    db.run_command(doc! {
        "dropUser": &username
    })
    .await?;

    Ok(())
}
