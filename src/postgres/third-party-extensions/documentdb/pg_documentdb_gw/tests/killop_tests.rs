pub mod common;
use mongodb::bson::doc;

#[tokio::test]
async fn validate_killop_missing_op_field() {
    let client = common::initialize().await;
    let db = client.database("admin");

    let result = db.run_command(doc! {"killOp": 1}).await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("Did not provide \"op\" field"),
                "Expected error to mention missing 'op' field, got: {msg}"
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn validate_killop_invalid_op_format_no_colon() {
    let client = common::initialize().await;
    let db = client.database("admin");

    let result = db.run_command(doc! {"killOp": 1, "op": "12345"}).await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("The op argument to killOp must be of the format shardid:opid"),
                "Expected error to mention format 'shardid:opid', got: {msg}"
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn validate_killop_invalid_shard_id() {
    let client = common::initialize().await;
    let db = client.database("admin");

    let result = db.run_command(doc! {"killOp": 1, "op": "foo:12345"}).await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("Invalid shardId"),
                "Expected error to mention invalid shardId, got: {msg}"
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn validate_killop_invalid_op_id() {
    let client = common::initialize().await;
    let db = client.database("admin");

    let result = db
        .run_command(doc! {"killOp": 1, "op": "12345:1234C5"})
        .await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("Invalid opId"),
                "Expected error to mention invalid opId, got: {msg}"
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn validate_killop_non_admin_database() {
    let client = common::initialize().await;
    let db = client.database("test");

    let result = db
        .run_command(doc! {"killOp": 1, "op": "10000000001:12345"})
        .await;

    match result {
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("killOp may only be run against the admin database."),
                "Expected error to mention admin database requirement, got: {msg}",
            );
        }
        Ok(_) => panic!("Expected error but got success"),
    }
}

#[tokio::test]
async fn validate_killop_valid_format() {
    let client = common::initialize().await;
    let db = client.database("admin");

    // This is a valid killOp command, but the operation doesn't exist
    // Should succeed with ok: 1.0 (it's a no-op for non-existent operations)
    let result = db
        .run_command(doc! {"killOp": 1, "op": "10000004122:12345"})
        .await
        .unwrap();

    // Verify the response has ok field
    assert!(result.contains_key("ok"));
    let ok_value = result.get_f64("ok").unwrap_or(0.0);
    assert_eq!(ok_value, 1.0, "Expected ok to be 1.0, got: {ok_value}");
}
