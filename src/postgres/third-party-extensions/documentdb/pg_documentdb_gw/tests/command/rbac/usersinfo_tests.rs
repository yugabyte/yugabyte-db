/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/command/rbac/usersinfo_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{doc, Bson, Document};
use uuid::Uuid;

pub mod common;
pub use crate::common::rbac_utils::user_exists;

#[tokio::test]
async fn test_users_info() -> Result<(), mongodb::error::Error> {
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
            "usersInfo": 1
        })
        .await?;

    assert!(
        !users.is_empty(),
        "non-empty users should be returned from usersInfo"
    );

    assert!(
        user_exists(&users, &user_id),
        "test user should be returned in usersInfo"
    );

    assert!(
        !user_exists(&users, "documentdb_bg_worker_role"),
        "documentdb_bg_worker_role should not be returned in usersInfo"
    );

    assert!(
        !user_exists(&users, "documentdb_admin_role"),
        "documentdb_admin_role should not be returned in usersInfo"
    );

    db.run_command(doc! {
        "dropUser": &username
    })
    .await?;

    Ok(())
}
