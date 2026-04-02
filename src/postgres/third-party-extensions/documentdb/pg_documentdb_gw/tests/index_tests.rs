/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/index_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{doc, Document};
use mongodb::IndexModel;

mod common;

#[tokio::test]
async fn create_index() {
    let db = common::initialize_with_db("create_index").await;

    let result = db
        .collection::<Document>("test")
        .create_index(IndexModel::builder().keys(doc! {"a":1}).build())
        .await
        .unwrap();
    assert_eq!(result.index_name, "a_1");

    let indexes = db
        .collection::<Document>("test")
        .list_index_names()
        .await
        .unwrap();
    assert_eq!(indexes.len(), 2);
}

#[tokio::test]
async fn create_list_drop_index() {
    let db = common::initialize_with_db("drop_indexes").await;

    let coll = db.collection("test");
    coll.insert_one(doc! {"a": 1}).await.unwrap();

    db.collection::<Document>("test")
        .create_index(IndexModel::builder().keys(doc! {"a":1}).build())
        .await
        .unwrap();

    db.collection::<Document>("test")
        .drop_indexes()
        .await
        .unwrap();

    coll.drop().await.unwrap();
}

#[tokio::test]
async fn create_index_with_long_name_should_fail() {
    let db = common::initialize_with_db("long_index_name").await;

    // MongoDB index name limit is 128 bytes
    let long_field_name = "a".repeat(1530);
    let index_model = IndexModel::builder()
        .keys(doc! { long_field_name.clone(): 1 })
        .build();

    let result = db
        .collection::<Document>("test")
        .create_index(index_model)
        .await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("The index path or expression is too long"),
                "Expected error containing 'The index path or expression is too long', got: {msg}"
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}
