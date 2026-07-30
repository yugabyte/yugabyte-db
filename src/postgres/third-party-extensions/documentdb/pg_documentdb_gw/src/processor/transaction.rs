/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/transaction.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::{
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDataClient,
    requests::{read_concern::ReadConcern, RequestType},
    responses::Response,
};

// Create the transaction if required, and populate the context information with the transaction info
pub async fn handle(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<()> {
    let (request, request_info, _) = request_context.get_components();

    connection_context.transaction = None;
    if let Some(request_transaction_info) = &request_info.transaction_info {
        if request_transaction_info.auto_commit {
            if request_info.session_id.is_none() {
                return Err(DocumentDBError::UntypedDocumentDBError(
                    50768,
                    "txnNumber may only be provided for multi-document transactions and retryable write commands. autocommit:false was not provided, and command is not a retryable write command.".to_string(),
                    String::new(),
                    std::backtrace::Backtrace::capture(),
                ));
            }

            return Ok(());
        }

        if (matches!(request.request_type(), RequestType::KillCursors)
            && request_transaction_info.start_transaction
            && !request_transaction_info.auto_commit)
        {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::OperationNotSupportedInTransaction,
                "Cannot run command KillCursors at the start of a transaction".to_string(),
            ));
        };

        if matches!(
            request.request_type(),
            RequestType::ReIndex
                | RequestType::CreateIndex
                | RequestType::CreateIndexes
                | RequestType::DropIndexes
        ) {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::OperationNotSupportedInTransaction,
                "Cannot run command in transaction.".to_string(),
            ));
        }

        if matches!(
            request.request_type(),
            RequestType::Aggregate
                | RequestType::FindAndModify
                | RequestType::Update
                | RequestType::Insert
                | RequestType::Count
                | RequestType::Distinct
                | RequestType::Find
                | RequestType::GetMore
        ) {
            if matches!(request.db()?, "config" | "admin" | "local") {
                return Err(DocumentDBError::documentdb_error(
                    ErrorCode::OperationNotSupportedInTransaction,
                    format!(
                        "Cannot perform data operation against database {} inside a transaction",
                        request.db()?
                    ),
                ));
            }

            let collection: &str = match request_info.collection() {
                Ok(c) if !c.is_empty() => c,
                _ => "",
            };

            if collection == "system.profile" {
                return Err(DocumentDBError::documentdb_error(
                    ErrorCode::OperationNotSupportedInTransaction,
                    "Cannot run command against system collections in transaction.".to_string(),
                ));
            }

            if collection.starts_with("system.") {
                return Err(DocumentDBError::UntypedDocumentDBError(
                    51071,
                    "Cannot run command against system views in transaction.".to_string(),
                    String::new(),
                    std::backtrace::Backtrace::capture(),
                ));
            }
        }

        if !request_transaction_info.start_transaction
            && *request_info.read_concern() != ReadConcern::Unspecified
        {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::InvalidOptions,
                "Read concern cannot be defined after transaction has started".to_string(),
            ));
        }

        if request_info.read_concern() == &ReadConcern::Snapshot
            && !(connection_context
                .service_context
                .dynamic_configuration()
                .allow_transaction_snapshot()
                .await)
        {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::CommandNotSupported,
                format!(
                    "'{:?}' read concern is not supported",
                    &ReadConcern::Snapshot
                ),
            ));
        }

        if matches!(
            request_info.read_concern(),
            ReadConcern::Available | ReadConcern::Linearizable
        ) {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::CommandNotSupported,
                format!(
                    "'{:?}' read concern is not supported",
                    request_info.read_concern()
                ),
            ));
        }

        let session_id = request_info
            .session_id
            .expect("Given that there's a transaction, there must be a session")
            .to_vec();
        let store = connection_context.service_context.transaction_store();
        let transaction_result = store
            .create(
                connection_context,
                request_transaction_info,
                session_id.clone(),
                pg_data_client,
            )
            .await;

        if let Err(e) = transaction_result {
            return match (request.request_type(), &e) {
                // Especially allow the transaction to remain unfilled if it is committing a committed transaction
                (
                    RequestType::CommitTransaction,
                    DocumentDBError::DocumentDBError(ErrorCode::TransactionCommitted, _, _),
                ) => Ok(()),
                _ => Err(e),
            };
        }

        connection_context.transaction =
            Some((session_id, request_transaction_info.transaction_number));
    }
    Ok(())
}

pub async fn process_commit(context: &mut ConnectionContext) -> Result<Response> {
    if let Some((session_id, _)) = context.transaction.as_ref() {
        let store = context.service_context.transaction_store();
        store.commit(session_id).await?;
    }
    Ok(Response::ok())
}

pub async fn process_abort(context: &mut ConnectionContext) -> Result<Response> {
    let (session_id, _) = context
        .transaction
        .as_ref()
        .ok_or(DocumentDBError::internal_error(
            "Transaction information was not populated for abort.".to_string(),
        ))?;

    let store = context.service_context.transaction_store();
    store.abort(session_id).await?;
    Ok(Response::ok())
}
