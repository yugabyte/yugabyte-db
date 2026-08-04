/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/command/rbac/dropuser_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::doc;
use uuid::Uuid;

pub mod common;
pub use crate::common::rbac_utils::user_exists;

#[tokio::test]
async fn test_drop_user() -> Result<(), mongodb::error::Error> {
    let client = common::initialize().await;
    let db_name = "admin";
    let db = client.database(db_name);
    let username = format!("user_{}", Uuid::new_v4().to_string().replace("-", ""));
    let user_id = format!("{}.{}", db_name, username);
    let role = "read";

    db.run_command(doc! {
        "createUser": &username,
        "pwd": "Valid$1Pass",
        "roles": [ { "role": role, "db": db_name } ]
    })
    .await?;

    let users_before = db
        .run_command(doc! {
            "usersInfo": &username
        })
        .await?;

    assert!(
        user_exists(&users_before, &user_id),
        "User should exist before drop"
    );

    db.run_command(doc! {
        "dropUser": &username
    })
    .await?;

    let users_after = db
        .run_command(doc! {
            "usersInfo": &username
        })
        .await?;

    assert!(
        !user_exists(&users_after, &user_id),
        "User should not exist after drop"
    );

    Ok(())
}

#[tokio::test]
async fn test_cannot_drop_system_users() -> Result<(), mongodb::error::Error> {
    let client = common::initialize().await;
    let db = client.database(db_name);

    let system_users = vec![
        "documentdb_bg_worker_role",
        "documentdb_admin_role",
        "documentdb_readonly_role",
    ];

    for user in system_users {
        common::utils::execute_command_and_validate_error(
            &db,
            doc! {
                "dropUser": user
            },
            2,
            "Invalid username.",
        )
        .await?;
    }

    Ok(())
}
