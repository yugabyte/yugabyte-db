/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/text_search_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::doc;
use documentdb_gateway::error::ErrorCode;

mod common;

#[tokio::test]
async fn text_query_should_fail_no_index() {
    let db = common::initialize_with_db("text_query_should_fail_no_index").await;
    db.create_collection("coll").await.unwrap();
    let collection = db.collection::<bson::Document>("coll");
    let filter = doc! { "$text": { "$search": "some search string" } };
    let result = collection.find(filter).await;

    match result {
        Err(e) => {
            if let mongodb::error::ErrorKind::Command(ref command_error) = *e.kind {
                let code_name = &command_error.code_name;
                assert_eq!(
                    "IndexNotFound", code_name,
                    "Expected codeName to be 'IndexNotFound', got: {code_name}"
                );

                let code = command_error.code;
                let expected_code = ErrorCode::IndexNotFound as i32;
                assert_eq!(
                    expected_code, code,
                    "Expected code to be {expected_code}, got: {code}",
                );

                let error_message = &command_error.message;
                assert_eq!(
                    "A text index is necessary to perform a $text query.",
                    error_message
                );
            } else {
                panic!("Expected Command error kind");
            }
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn text_query_exceed_max_depth() {
    let db = common::initialize_with_db("text_query_exceed_max_depth").await;

    let collection = db.collection::<bson::Document>("test");

    let index_result = db
        .collection::<bson::Document>("test")
        .create_index(
            mongodb::IndexModel::builder()
                .keys(doc! {"a":"text"})
                .build(),
        )
        .await
        .unwrap();
    assert_eq!(index_result.index_name, "a_text");

    let indexes = db
        .collection::<bson::Document>("test")
        .list_index_names()
        .await
        .unwrap();
    assert_eq!(indexes.len(), 2);

    // 32 levels of nested $text, should work
    let suc_filter = doc! { "$text": { "$search": "--------------------------------dummy" } };
    let suc_result = collection.find(suc_filter).await;
    assert!(suc_result.is_ok());

    // 33 levels of nested $text, exceeding the max depth of 32
    let filter = doc! { "$text": { "$search": "---------------------------------dummy" } };
    let result = collection.find(filter).await;

    match result {
        Err(e) => {
            if let mongodb::error::ErrorKind::Command(ref command_error) = *e.kind {
                let code = command_error.code;
                let expected_code = ErrorCode::BadValue as i32;
                assert_eq!(
                    expected_code, code,
                    "Expected code to be {expected_code}, got: {code}",
                );

                let error_message = &command_error.message;
                assert_eq!(
                    "$text query is exceeding the maximum allowed depth(32), please simplify the query",
                    error_message
                );
            } else {
                panic!("Expected Command error kind");
            }
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}
