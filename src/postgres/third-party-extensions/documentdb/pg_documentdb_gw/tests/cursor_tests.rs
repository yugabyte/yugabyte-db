/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/cursor_tests.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::doc;

pub mod common;

/*
 * Verify the batch size is honored
 * Verify cursor completeness
*/
#[tokio::test]
pub async fn validate_batch_size() {
    let db = common::initialize_with_db("cursor_tests_batch_size").await;
    let coll = db.collection("test");
    coll.insert_many((0..6).map(|_| doc! { "a": 1 }))
        .await
        .unwrap();

    let aggregate_result = db
        .run_command(doc! {
            "aggregate": "test",
            "pipeline": [{ "$match": { "a": 1 } }],
            "cursor": { "batchSize": 2 }
        })
        .await
        .unwrap();

    let cursor_doc = aggregate_result.get_document("cursor").unwrap();
    let first_batch = cursor_doc.get_array("firstBatch").unwrap();
    assert_eq!(
        first_batch.len(),
        2,
        "First batch should contain 2 documents"
    );

    let mut cursor_id = cursor_doc.get_i64("id").unwrap();
    assert_ne!(cursor_id, 0, "Cursor ID should not be 0");

    let mut total_documents = first_batch.len();
    let mut iterations = 1;
    let batch_size = 2;

    // Iterate through remaining documents
    while cursor_id != 0 {
        let result = db
            .run_command(doc! {
                "getMore": cursor_id,
                "collection": "test",
                "batchSize": batch_size
            })
            .await
            .unwrap();

        let cursor_doc = result.get_document("cursor").unwrap();
        let next_batch = cursor_doc.get_array("nextBatch").unwrap();

        total_documents += next_batch.len();
        iterations += 1;

        cursor_id = cursor_doc.get_i64("id").unwrap();
    }

    assert_eq!(total_documents, 6, "Should retrieve all 6 documents");
    assert_eq!(iterations, 3, "Expected 3 iterations");
}

/*
 * Verify the default batch size of 101
 * Verify killCursors
*/
#[tokio::test]
pub async fn test_cursor_default_batch_size() {
    let db = common::initialize_with_db("cursor_tests_101_kill").await;
    let coll = db.collection::<bson::Document>("test");

    // Insert 150 documents
    let docs: Vec<bson::Document> = (0..150).map(|i| doc! { "a": i }).collect();
    coll.insert_many(docs).await.unwrap();

    // Get 101 documents in first batch (default batch size)
    let aggregate_result = db
        .run_command(doc! {
            "aggregate": "test",
            "pipeline": [{ "$match": { "a": { "$gte": 0 } } }],
            "cursor": {}
        })
        .await
        .unwrap();

    let cursor_doc = aggregate_result.get_document("cursor").unwrap();
    let first_batch = cursor_doc.get_array("firstBatch").unwrap();
    assert_eq!(
        first_batch.len(),
        101,
        "First batch should contain 101 documents"
    );

    let cursor_id = cursor_doc.get_i64("id").unwrap();
    assert_ne!(cursor_id, 0, "Cursor ID should not be 0");

    // Kill the cursor
    let kill_cursors_result = db
        .run_command(doc! {
            "killCursors": "test",
            "cursors": [cursor_id]
        })
        .await
        .unwrap();

    let cursors_killed = kill_cursors_result.get_array("cursorsKilled").unwrap();
    assert_eq!(cursors_killed.len(), 1, "Should kill 1 cursor");

    let get_more_results = db
        .run_command(doc! {
            "getMore": cursor_id,
            "collection": "test"
        })
        .await;

    assert!(
        get_more_results.is_err(),
        "getMore should fail on killed cursor"
    );

    if let Err(e) = get_more_results {
        if let mongodb::error::ErrorKind::Command(ref cmd_err) = *e.kind {
            assert_eq!(
                cmd_err.code, 43,
                "Expected CursorNotFound error code 43, but got {}",
                cmd_err.code
            );
            assert!(
                cmd_err.message.contains("Provided cursor was not found."),
                "Error message should indicate cursor not found, got: {}",
                cmd_err.message
            );
        } else {
            panic!("Expected Command error, but got: {:?}", e.kind);
        }
    }
}

/*
 * Verify killCursors with multiple cursors
 * Verify killCursors with non-existing cursor
*/
#[tokio::test]
pub async fn test_cursor_kill_multiple_cursors() {
    let db = common::initialize_with_db("cursor_tests_kill_multiple").await;
    let coll = db.collection::<bson::Document>("test");

    // Insert 150 documents
    let docs: Vec<bson::Document> = (0..150).map(|i| doc! { "a": i }).collect();
    coll.insert_many(docs).await.unwrap();

    // Create first cursor
    let aggregate_result1 = db
        .run_command(doc! {
            "aggregate": "test",
            "pipeline": [{ "$match": { "a": { "$gte": 0 } } }],
            "cursor": { "batchSize": 10 }
        })
        .await
        .unwrap();

    let cursor_id1 = aggregate_result1
        .get_document("cursor")
        .unwrap()
        .get_i64("id")
        .unwrap();
    assert_ne!(cursor_id1, 0, "First cursor ID should not be 0");

    // Create second cursor
    let aggregate_result2 = db
        .run_command(doc! {
            "aggregate": "test",
            "pipeline": [{ "$match": { "a": { "$gte": 0 } } }],
            "cursor": { "batchSize": 20 }
        })
        .await
        .unwrap();

    let cursor_id2 = aggregate_result2
        .get_document("cursor")
        .unwrap()
        .get_i64("id")
        .unwrap();
    assert_ne!(cursor_id2, 0, "Second cursor ID should not be 0");

    // Kill multiple cursors at once, including a non-existent cursor ID in the middle
    let invalid_cursor_id: i64 = 9999999;
    let kill_cursors_result1 = db
        .run_command(doc! {
            "killCursors": "test",
            "cursors": [cursor_id1, invalid_cursor_id, cursor_id2]
        })
        .await
        .unwrap();

    let cursors_killed = kill_cursors_result1.get_array("cursorsKilled").unwrap();
    assert_eq!(
        cursors_killed.len(),
        2,
        "Should kill 2 cursors (non-existent cursor is silently ignored)"
    );

    for (i, cursor_id) in [cursor_id1, cursor_id2].iter().enumerate() {
        let get_more_result = db
            .run_command(doc! {
                "getMore": cursor_id,
                "collection": "test"
            })
            .await;

        assert!(
            get_more_result.is_err(),
            "getMore should fail on killed cursor at index {i}"
        );
    }
}
