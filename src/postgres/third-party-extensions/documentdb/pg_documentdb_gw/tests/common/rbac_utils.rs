/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/common/rbac_utils.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{Bson, Document};

#[allow(dead_code)]
pub fn user_exists(doc: &Document, expected_user_id: &str) -> bool {
    if let Ok(users) = doc.get_array("users") {
        for user in users {
            if let Some(user_doc) = user.as_document() {
                if let Some(id) = user_doc.get("_id").and_then(Bson::as_str) {
                    if id == expected_user_id {
                        return true;
                    }
                }
            }
        }
    }
    false
}

#[allow(dead_code)]
pub fn validate_user(
    doc: &Document,
    expected_user_id: &str,
    expected_user: &str,
    expected_db: &str,
    expected_role: &str,
) {
    let users = doc.get_array("users").expect("users array should exist");

    let mut user_found = false;
    for user in users {
        if let Some(user_doc) = user.as_document() {
            if let Some(id) = user_doc.get("_id").and_then(Bson::as_str) {
                if id == expected_user_id {
                    user_found = true;

                    let user_name = user_doc
                        .get("user")
                        .and_then(Bson::as_str)
                        .expect("user field should exist");
                    assert_eq!(user_name, expected_user, "user name mismatch");

                    let db = user_doc
                        .get("db")
                        .and_then(Bson::as_str)
                        .expect("db field should exist");
                    assert_eq!(db, expected_db, "database name mismatch");

                    let roles = user_doc
                        .get_array("roles")
                        .expect("roles array should exist");
                    let mut role_found = false;

                    for role in roles {
                        if let Some(role_doc) = role.as_document() {
                            if let Some(role_name) = role_doc.get("role").and_then(Bson::as_str) {
                                if role_name == expected_role {
                                    role_found = true;
                                    if let Some(role_db) = role_doc.get("db").and_then(Bson::as_str)
                                    {
                                        assert_eq!(role_db, expected_db, "role database mismatch");
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    assert!(role_found, "expected role '{expected_role}' not found");
                }
            }
        }
    }
    assert!(user_found, "user with id '{expected_user_id}' not found");
}
