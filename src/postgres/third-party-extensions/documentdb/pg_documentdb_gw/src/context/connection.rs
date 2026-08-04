/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/context/connection.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    hash::{DefaultHasher, Hash, Hasher},
    sync::Arc,
    time::{Duration, Instant},
};

use bson::RawDocumentBuf;
use openssl::ssl::SslRef;
use uuid::{Builder, Uuid};

use crate::{
    auth::AuthState,
    configuration::DynamicConfiguration,
    context::{Cursor, CursorStoreEntry, ServiceContext},
    error::{DocumentDBError, Result},
    postgres::Connection,
    telemetry::TelemetryProvider,
};

pub struct ConnectionContext {
    pub start_time: Instant,
    pub connection_id: Uuid,
    pub service_context: Arc<ServiceContext>,
    pub auth_state: AuthState,
    pub requires_response: bool,
    pub client_information: Option<RawDocumentBuf>,
    pub transaction: Option<(Vec<u8>, i64)>,
    pub telemetry_provider: Option<Box<dyn TelemetryProvider>>,
    pub ip_address: String,
    pub cipher_type: i32,
    pub ssl_protocol: String,
    transport_protocol: String,
    connection_id_hash: i32,
}

impl ConnectionContext {
    pub fn new(
        service_context: ServiceContext,
        telemetry_provider: Option<Box<dyn TelemetryProvider>>,
        ip_address: String,
        tls_config: Option<&SslRef>,
        connection_id: Uuid,
        transport_protocol: String,
    ) -> Self {
        let tls_provider = service_context.tls_provider();

        let cipher_type = tls_config
            .map(|tls| tls_provider.ciphersuite_to_i32(tls.current_cipher()))
            .unwrap_or_default();

        let ssl_protocol = tls_config
            .map(|tls| tls.version_str().to_string())
            .unwrap_or_default();

        ConnectionContext {
            start_time: Instant::now(),
            connection_id,
            service_context: Arc::new(service_context),
            auth_state: AuthState::new(),
            requires_response: true,
            client_information: None,
            transaction: None,
            telemetry_provider,
            ip_address,
            cipher_type,
            ssl_protocol,
            transport_protocol,
            connection_id_hash: Self::get_uuid_hash(connection_id),
        }
    }

    pub async fn get_cursor(&self, id: i64, username: &str) -> Option<CursorStoreEntry> {
        // If there is a transaction, get the cursor to its store
        if let Some((session_id, _)) = self.transaction.as_ref() {
            let transaction_store = self.service_context.transaction_store();
            if let Some((_, transaction)) =
                transaction_store.transactions.read().await.get(session_id)
            {
                return transaction
                    .cursors
                    .get_cursor((id, username.to_string()))
                    .await;
            }
        }

        self.service_context
            .cursor_store()
            .get_cursor((id, username.to_string()))
            .await
    }

    #[allow(clippy::too_many_arguments)]
    pub async fn add_cursor(
        &self,
        conn: Option<Arc<Connection>>,
        cursor: Cursor,
        username: &str,
        db: &str,
        collection: &str,
        cursor_timeout: Duration,
        session_id: Option<Vec<u8>>,
    ) {
        let key = (cursor.cursor_id, username.to_string());
        let value = CursorStoreEntry {
            conn,
            cursor,
            db: db.to_string(),
            collection: collection.to_string(),
            timestamp: Instant::now(),
            cursor_timeout,
            session_id,
        };

        // If there is a transaction, add the cursor to its store
        if let Some((session_id, _)) = self.transaction.as_ref() {
            let transaction_store = self.service_context.transaction_store();
            if let Some((_, transaction)) =
                transaction_store.transactions.read().await.get(session_id)
            {
                transaction.cursors.add_cursor(key, value).await;
                return;
            }
        }

        // Otherwise add it to the service context
        self.service_context
            .cursor_store()
            .add_cursor(key, value)
            .await
    }

    pub async fn allocate_data_pool(&self) -> Result<()> {
        let username = self.auth_state.username()?;
        let password = self
            .auth_state
            .password
            .as_ref()
            .ok_or(DocumentDBError::internal_error(
                "Password is missing on pg connection acquisition".to_string(),
            ))?;

        self.service_context
            .connection_pool_manager()
            .allocate_data_pool(username, password)
            .await
    }

    pub fn dynamic_configuration(&self) -> Arc<dyn DynamicConfiguration> {
        self.service_context.dynamic_configuration()
    }

    /// Generates a per-request activity ID by embedding the given `request_id`
    /// into the caller’s connection UUID and returning it as a hyphenated string.
    ///
    /// The function copies the current `connection_id` (a 16-byte UUID), overwrites
    /// bytes 12..16 (the final 4 bytes) with `request_id.to_be_bytes()`
    /// to preserve UUID version/variant bits, then returns the resulting UUID’s
    /// canonical (lowercase, hyphenated) string form.
    ///
    /// # Parameters
    /// - `request_id`: 32-bit identifier to embed (stored big-endian in bytes 12–15).
    ///
    /// # Returns
    /// A `String` containing the new UUID, e.g. `"550e8400-e29b-41d4-a716-446655440000"`.
    pub fn generate_request_activity_id(&mut self, request_id: i32) -> String {
        let mut activity_id_bytes = *self.connection_id.as_bytes();
        activity_id_bytes[12..].copy_from_slice(&request_id.to_be_bytes());
        Builder::from_bytes(activity_id_bytes)
            .into_uuid()
            .to_string()
    }

    pub fn transport_protocol(&self) -> &str {
        &self.transport_protocol
    }

    pub fn get_connection_id_hash(&self) -> i32 {
        self.connection_id_hash
    }

    /// Returns a non-negative 32-bit hash for `self.connection_id`.
    ///
    /// Implementation details:
    /// - Hashes `connection_id` with `DefaultHasher` to a 64-bit value.
    /// - Folds to 32 bits by XORing high and low halves.
    /// - Masks off the sign bit (`& 0x7fff_ffff`) so the result fits in `0..=i31::MAX`.
    fn get_uuid_hash(connection_id: Uuid) -> i32 {
        let mut hasher = DefaultHasher::new();
        connection_id.hash(&mut hasher);
        let finished_hash = hasher.finish();
        ((finished_hash ^ (finished_hash >> 32)) & 0x7fff_ffff) as i32
    }
}
