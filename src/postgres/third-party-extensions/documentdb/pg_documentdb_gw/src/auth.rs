/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/auth.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{str::from_utf8, sync::Arc};

use crate::{
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::{PgDataClient, PgDocument},
    processor,
    protocol::OK_SUCCEEDED,
    requests::{request_tracker::RequestTracker, Request, RequestType},
    responses::{PgResponse, RawResponse, Response},
};
use base64::{engine::general_purpose, Engine as _};
use bson::{rawdoc, spec::BinarySubtype};
use rand::Rng;
use serde_json::Value;
use tokio::{
    sync::RwLock,
    time::{sleep, Duration},
};
use tokio_postgres::{error::SqlState, types::Type};
const NONCE_LENGTH: usize = 2;

#[derive(Debug, Clone, PartialEq)]
pub enum AuthKind {
    Native,
    ExternalIdentity,
}

pub struct ScramFirstState {
    nonce: String,
    first_message_bare: String,
    first_message: String,
}

pub struct AuthState {
    authorized: Arc<RwLock<bool>>,
    first_state: Option<ScramFirstState>,
    username: Option<String>,
    pub password: Option<String>,
    user_oid: Option<u32>,
    auth_kind: Option<AuthKind>,
    timer_initialized: Arc<RwLock<bool>>,
}

impl Default for AuthState {
    fn default() -> Self {
        Self::new()
    }
}

impl AuthState {
    pub fn new() -> Self {
        AuthState {
            authorized: Arc::new(RwLock::new(false)),
            first_state: None,
            username: None,
            password: None,
            user_oid: None,
            auth_kind: None,
            timer_initialized: Arc::new(RwLock::new(false)),
        }
    }

    pub fn username(&self) -> Result<&str> {
        self.username
            .as_deref()
            .ok_or(DocumentDBError::internal_error(
                "Username missing".to_string(),
            ))
    }

    pub fn user_oid(&self) -> Result<u32> {
        self.user_oid.ok_or(DocumentDBError::internal_error(
            "User OID missing".to_string(),
        ))
    }

    pub fn is_authorized(&self) -> Arc<RwLock<bool>> {
        Arc::clone(&self.authorized)
    }

    pub fn auth_kind(&self) -> &Option<AuthKind> {
        &self.auth_kind
    }

    pub fn set_username(&mut self, user: &str) {
        self.username = Some(user.to_string());
    }

    pub fn set_user_oid(&mut self, user_oid: u32) {
        self.user_oid = Some(user_oid);
    }

    pub fn set_auth_kind(&mut self, kind: AuthKind) -> Result<()> {
        if self.auth_kind.is_none() {
            self.auth_kind = Some(kind);
            Ok(())
        } else if self.auth_kind != Some(kind) {
            Err(DocumentDBError::internal_error(
                "Auth kind is already set".to_string(),
            ))
        } else {
            Ok(())
        }
    }

    async fn initialize_expiry_timer(
        &mut self,
        timeout_secs: u64,
        connection_activity_id: &str,
    ) -> Result<()> {
        let timer_initialized = Arc::clone(&self.timer_initialized);
        if *timer_initialized.read().await {
            return Err(DocumentDBError::internal_error(
                "Authentication expiry timer is already initialized".to_string(),
            ));
        }

        let authorized = Arc::clone(&self.authorized);
        let connection_activity_id_owned = connection_activity_id.to_string();

        // Spawn new expiry task that counts down and sets authorized to false
        tokio::spawn(async move {
            *timer_initialized.write().await = true;

            sleep(Duration::from_secs(timeout_secs)).await;

            let connection_activity_id_as_str = connection_activity_id_owned.as_str();
            tracing::info!(
                activity_id = connection_activity_id_as_str,
                "Authentication expiry timer elapsed"
            );
            *authorized.write().await = false;
            *timer_initialized.write().await = false;
        });

        Ok(())
    }
}

pub async fn process<T>(
    connection_context: &mut ConnectionContext,
    request_context: &mut RequestContext<'_>,
) -> Result<Response>
where
    T: PgDataClient,
{
    let request = request_context.payload;
    if let Some(response) = handle_auth_request(connection_context, request).await? {
        return Ok(response);
    }

    if request.request_type().allowed_unauthorized() {
        let service_context = Arc::clone(&connection_context.service_context);
        let data_client = T::new_unauthorized(&service_context).await?;

        return processor::process_request(request_context, connection_context, data_client).await;
    }

    Err(DocumentDBError::unauthorized(format!(
        "Command {} is not allowed as the connection is not authenticated yet.",
        request.request_type().to_string().to_lowercase()
    )))
}

async fn handle_auth_request(
    connection_context: &mut ConnectionContext,
    request: &Request<'_>,
) -> Result<Option<Response>> {
    match request.request_type() {
        RequestType::SaslStart => Ok(Some(handle_sasl_start(connection_context, request).await?)),
        RequestType::SaslContinue => Ok(Some(
            handle_sasl_continue(connection_context, request).await?,
        )),
        RequestType::Logout => {
            connection_context.auth_state = AuthState::new();
            Ok(Some(Response::Raw(RawResponse(rawdoc! {
                "ok": OK_SUCCEEDED,
            }))))
        }
        _ => Ok(None),
    }
}

fn generate_server_nonce(client_nonce: &str) -> String {
    const CHARSET: &[u8] = b"!\"#$%&'()*+-./0123456789:;<>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    let mut rng = rand::thread_rng();

    let mut result = String::with_capacity(NONCE_LENGTH);
    for _ in 0..NONCE_LENGTH {
        let idx = rng.gen_range(0..CHARSET.len());
        result.push(CHARSET[idx] as char);
    }

    format!("{client_nonce}{result}")
}

async fn handle_sasl_start(
    connection_context: &mut ConnectionContext,
    request: &Request<'_>,
) -> Result<Response> {
    let mechanism = request
        .document()
        .get_str("mechanism")
        .map_err(DocumentDBError::parse_failure())?;

    if mechanism != "SCRAM-SHA-256" && mechanism != "MONGODB-OIDC" {
        return Err(DocumentDBError::authentication_failed(format!(
            "Only SCRAM-SHA-256 and MONGODB-OIDC are supported, got: {mechanism}"
        )));
    }

    if mechanism == "MONGODB-OIDC" {
        return handle_oidc(connection_context, request).await;
    } else {
        return handle_scram(connection_context, request).await;
    }
}

async fn handle_scram(
    connection_context: &mut ConnectionContext,
    request: &Request<'_>,
) -> Result<Response> {
    let payload = parse_sasl_payload(request, true)?;

    let username = payload
        .username
        .ok_or(DocumentDBError::authentication_failed(
            "Username missing from SaslStart.".to_string(),
        ))?;

    let client_nonce = payload.nonce.ok_or(DocumentDBError::authentication_failed(
        "Nonce missing from SaslStart.".to_string(),
    ))?;

    let server_nonce = generate_server_nonce(client_nonce);

    let (salt, iterations) = get_salt_and_iteration(connection_context, username).await?;
    let response = format!("r={server_nonce},s={salt},i={iterations}");

    connection_context.auth_state.first_state = Some(ScramFirstState {
        nonce: server_nonce,
        first_message_bare: format!("n={username},r={client_nonce}"),
        first_message: response.clone(),
    });

    connection_context.auth_state.username = Some(username.to_string());

    connection_context
        .auth_state
        .set_auth_kind(AuthKind::Native)?;

    let binary_response = bson::Binary {
        subtype: BinarySubtype::Generic,
        bytes: response.as_bytes().to_vec(),
    };

    Ok(Response::Raw(RawResponse(rawdoc! {
        "payload": binary_response,
        "ok": OK_SUCCEEDED,
        "conversationId": 1,
        "done": false
    })))
}

async fn handle_oidc(
    connection_context: &mut ConnectionContext,
    request: &Request<'_>,
) -> Result<Response> {
    let payload = request
        .document()
        .get_binary("payload")
        .map_err(DocumentDBError::parse_failure())?;

    let payload_doc = bson::Document::from_reader(&mut std::io::Cursor::new(payload.bytes))
        .map_err(|e| {
            DocumentDBError::bad_value(format!("Failed to parse OIDC payload as BSON: {e}"))
        })?;

    let jwt_token = payload_doc.get_str("jwt").map_err(|_| {
        DocumentDBError::authentication_failed("JWT token missing from OIDC payload".to_string())
    })?;

    handle_oidc_token_authentication(connection_context, jwt_token).await
}

async fn handle_oidc_token_authentication(
    connection_context: &mut ConnectionContext,
    token_string: &str,
) -> Result<Response> {
    let (oid, seconds_until_expiry) = parse_and_validate_jwt_token(token_string)?;

    let connection = connection_context
        .service_context
        .connection_pool_manager()
        .authentication_connection()
        .await?;

    let authentication_token_row = match connection
        .query(
            connection_context
                .service_context
                .query_catalog()
                .authenticate_with_token(),
            &[Type::TEXT, Type::TEXT],
            &[&oid, &token_string],
            None,
            &mut RequestTracker::new(),
        )
        .await
    {
        Ok(rows) => rows,
        Err(e) => {
            match e {
                DocumentDBError::PostgresError(pge_error, _) => match pge_error.as_db_error() {
                    Some(db_error) => {
                        tracing::error!(
                            activity_id = connection_context.connection_id.to_string().as_str(),
                            "Backend error during authentication: PostgresError({:?}, {:?})",
                            db_error.code(),
                            db_error.hint()
                        );

                        if let Some((extension_error_code, _)) =
                            PgResponse::from_known_external_error_code(db_error.code())
                        {
                            if extension_error_code == ErrorCode::CommandNotSupported as i32 {
                                return Err(DocumentDBError::authentication_failed(
                                    "The authentication mechanism provided is not supported in the service.".to_string(),
                                ));
                            }
                        }

                        return match *db_error.code() {
                            SqlState::INVALID_PASSWORD => {
                                Err(DocumentDBError::authentication_failed(
                                    "The token provided is not valid.".to_string(),
                                ))
                            }
                            SqlState::UNDEFINED_OBJECT => {
                                Err(DocumentDBError::authentication_failed(
                                    "External identity is not present in the system.".to_string(),
                                ))
                            }
                            // All other errors are returned as InternalError in authentication code path.
                            _ => Err(DocumentDBError::authentication_failed(
                                "Internal Error.".to_string(),
                            )),
                        };
                    }
                    None => {
                        tracing::error!(
                            activity_id = connection_context.connection_id.to_string().as_str(),
                            "DbError not found in PostgresError, which is unexpected."
                        );
                        return Err(DocumentDBError::authentication_failed(
                            "Internal Error.".to_string(),
                        ));
                    }
                },
                _ => return Err(e),
            }
        }
    };

    let authentication_result: String = authentication_token_row
        .first()
        .ok_or(DocumentDBError::pg_response_empty())?
        .try_get(0)?;

    if authentication_result.trim() != oid {
        return Err(DocumentDBError::authentication_failed(
            "Token validation failed".to_string(),
        ));
    }

    let server_signature = "";
    let payload = bson::Binary {
        subtype: BinarySubtype::Generic,
        bytes: server_signature.as_bytes().to_vec(),
    };

    connection_context.auth_state.set_username(&oid);
    connection_context.auth_state.password = Some(token_string.to_string());
    connection_context.auth_state.user_oid = Some(get_user_oid(connection_context, &oid).await?);

    *connection_context.auth_state.is_authorized().write().await = true;
    connection_context
        .auth_state
        .set_auth_kind(AuthKind::ExternalIdentity)?;

    /* We are setting a timer for the time until token expiry, which will set authorized to false at the end */
    let connection_activity_id = connection_context.connection_id.to_string();
    let connection_activity_id_as_str = connection_activity_id.as_str();
    tracing::info!(activity_id = connection_activity_id_as_str,
        "Setting authentication expiry timer for {seconds_until_expiry} seconds until token expiry.",
    );
    connection_context
        .auth_state
        .initialize_expiry_timer(seconds_until_expiry, connection_activity_id_as_str)
        .await?;

    Ok(Response::Raw(RawResponse(rawdoc! {
        "payload": payload,
        "ok": OK_SUCCEEDED,
        "conversationId": 1,
        "done": true
    })))
}

fn parse_and_validate_jwt_token(token_string: &str) -> Result<(String, u64)> {
    let token_parts: Vec<&str> = token_string.split('.').collect();
    if token_parts.len() != 3 {
        return Err(DocumentDBError::authentication_failed(
            "Invalid JWT token format.".to_string(),
        ));
    }

    let payload_part = token_parts[1];
    let payload_bytes = general_purpose::URL_SAFE_NO_PAD
        .decode(payload_part)
        .map_err(|_| {
            DocumentDBError::authentication_failed("Invalid JWT token encoding.".to_string())
        })?;

    let payload_json: Value = serde_json::from_slice(&payload_bytes).map_err(|_| {
        DocumentDBError::authentication_failed("Invalid JWT token payload.".to_string())
    })?;

    let oid = payload_json
        .get("oid")
        .and_then(|v| v.as_str())
        .ok_or_else(|| {
            DocumentDBError::authentication_failed("Token does not contain OID.".to_string())
        })?
        .to_string();

    let aud = payload_json
        .get("aud")
        .and_then(|v| v.as_str())
        .ok_or_else(|| {
            DocumentDBError::authentication_failed(
                "Token does not contain audience claim.".to_string(),
            )
        })?
        .to_string();

    let exp = payload_json
        .get("exp")
        .and_then(|v| v.as_i64())
        .ok_or_else(|| {
            DocumentDBError::authentication_failed(
                "Token does not contain expiry time.".to_string(),
            )
        })?;

    let valid_audiences = ["https://ossrdbms-aad.database.windows.net"];
    if !valid_audiences.contains(&aud.as_str()) {
        return Err(DocumentDBError::authentication_failed(
            "The audience claim provided in the token is not valid.".to_string(),
        ));
    }

    let exp_datetime = std::time::UNIX_EPOCH + std::time::Duration::from_secs(exp as u64);
    let now = std::time::SystemTime::now();

    if exp_datetime < now {
        return Err(DocumentDBError::authentication_failed(
            "The token provided is expired.".to_string(),
        ));
    }

    let timeout_seconds = exp_datetime
        .duration_since(now)
        .unwrap_or(Duration::from_secs(0))
        .as_secs();

    Ok((oid, timeout_seconds))
}

async fn handle_sasl_continue(
    connection_context: &mut ConnectionContext,
    request: &Request<'_>,
) -> Result<Response> {
    let payload = parse_sasl_payload(request, false)?;

    if let Some(first_state) = connection_context.auth_state.first_state.as_ref() {
        let mechanism_result = request.document().get_str("mechanism");

        // Only validate mechanism if it's present - it's optional in SaslContinue
        if let Ok(mechanism) = mechanism_result {
            if mechanism == "MONGODB-OIDC" {
                return Err(DocumentDBError::authentication_failed(
                    "Auth mechanism MONGODB-OIDC is not supported in SaslContinue".to_string(),
                ));
            }
        } else {
            tracing::warn!("Auth mechanism not provided in SaslContinue");
        }

        // Username is not always provided by saslcontinue

        let client_nonce = payload.nonce.ok_or(DocumentDBError::authentication_failed(
            "Nonce missing from SaslContinue.".to_string(),
        ))?;
        let proof = payload.proof.ok_or(DocumentDBError::authentication_failed(
            "Proof missing from SaslContinue.".to_string(),
        ))?;
        let channel_binding =
            payload
                .channel_binding
                .ok_or(DocumentDBError::authentication_failed(
                    "Channel binding missing from SaslContinue.".to_string(),
                ))?;
        let username = payload
            .username
            .or(connection_context.auth_state.username.as_deref())
            .ok_or(DocumentDBError::internal_error(
                "Username missing from SaslContinue".to_string(),
            ))?;

        if client_nonce != first_state.nonce {
            return Err(DocumentDBError::authentication_failed(
                "Nonce did not match expected nonce.".to_string(),
            ));
        }

        let auth_message = format!(
            "{},{},c={},r={}",
            first_state.first_message_bare,
            first_state.first_message,
            channel_binding,
            client_nonce
        );

        let scram_sha256_row = connection_context
            .service_context
            .connection_pool_manager()
            .authentication_connection()
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .authenticate_with_scram_sha256(),
                &[Type::TEXT, Type::TEXT, Type::TEXT],
                &[&username, &auth_message, &proof],
                None,
                &mut RequestTracker::new(),
            )
            .await?;

        let scram_sha256_doc: PgDocument = scram_sha256_row
            .first()
            .ok_or(DocumentDBError::pg_response_empty())?
            .try_get(0)?;

        if scram_sha256_doc
            .0
            .get_i32("ok")
            .map_err(DocumentDBError::pg_response_invalid)?
            != 1
        {
            return Err(DocumentDBError::authentication_failed(
                "Invalid key".to_string(),
            ));
        }

        let server_signature = scram_sha256_doc
            .0
            .get_str("ServerSignature")
            .map_err(DocumentDBError::pg_response_invalid)?;

        let payload = bson::Binary {
            subtype: BinarySubtype::Generic,
            bytes: format!("v={server_signature}").as_bytes().to_vec(),
        };

        connection_context.auth_state.password = Some("".to_string());
        connection_context.auth_state.user_oid =
            Some(get_user_oid(connection_context, username).await?);

        *connection_context.auth_state.is_authorized().write().await = true;

        Ok(Response::Raw(RawResponse(rawdoc! {
            "payload": payload,
            "ok": OK_SUCCEEDED,
            "conversationId": 1,
            "done": true
        })))
    } else {
        Err(DocumentDBError::authentication_failed(
            "SaslContinue called without SaslStart state.".to_string(),
        ))
    }
}

struct ScramPayload<'a> {
    username: Option<&'a str>,
    nonce: Option<&'a str>,
    proof: Option<&'a str>,
    channel_binding: Option<&'a str>,
}

fn parse_sasl_payload<'a, 'b: 'a>(
    request: &'b Request<'a>,
    with_header: bool,
) -> Result<ScramPayload<'a>> {
    let payload = request
        .document()
        .get_binary("payload")
        .map_err(DocumentDBError::parse_failure())?;
    let mut payload = from_utf8(payload.bytes).map_err(|e| {
        DocumentDBError::bad_value(format!("Sasl payload couldn't be converted to utf-8: {e}"))
    })?;

    if with_header {
        if payload.len() < 3 {
            return Err(DocumentDBError::sasl_payload_invalid());
        }
        match &payload[0..=2] {
            "n,," => (),
            "p,," => (),
            "y,," => (),
            _ => return Err(DocumentDBError::sasl_payload_invalid()),
        }
        payload = &payload[3..];
    }

    let mut username: Option<&str> = None;
    let mut nonce: Option<&str> = None;
    let mut proof: Option<&str> = None;
    let mut channel_binding: Option<&str> = None;

    for value in payload.split(',') {
        let idx = value
            .find('=')
            .ok_or(DocumentDBError::sasl_payload_invalid())?;

        let k = &value[..idx];
        let v = &value[idx + 1..];
        match k {
            "n" => username = Some(v),
            "r" => nonce = Some(v),
            "p" => proof = Some(v),
            "c" => channel_binding = Some(v),
            _ => {
                return Err(DocumentDBError::authentication_failed(
                    "Sasl payload was invalid.".to_string(),
                ))
            }
        }
    }

    Ok(ScramPayload {
        username,
        nonce,
        proof,
        channel_binding,
    })
}

async fn get_salt_and_iteration(
    connection_context: &ConnectionContext,
    username: &str,
) -> Result<(String, i32)> {
    for blocked_prefix in connection_context
        .service_context
        .setup_configuration()
        .blocked_role_prefixes()
    {
        if username
            .to_lowercase()
            .starts_with(&blocked_prefix.to_lowercase())
        {
            return Err(DocumentDBError::authentication_failed(
                "Username is invalid.".to_string(),
            ));
        }
    }

    let results = connection_context
        .service_context
        .connection_pool_manager()
        .authentication_connection()
        .await?
        .query(
            connection_context
                .service_context
                .query_catalog()
                .salt_and_iterations(),
            &[Type::TEXT],
            &[&username],
            None,
            &mut RequestTracker::new(),
        )
        .await?;

    let doc: PgDocument = results
        .first()
        .ok_or(DocumentDBError::pg_response_empty())?
        .try_get(0)?;
    if doc
        .0
        .get_i32("ok")
        .map_err(|e| DocumentDBError::internal_error(e.to_string()))?
        != 1
    {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::AuthenticationFailed,
            "Invalid account: User details not found in the database".to_string(),
        ));
    }

    let iterations = doc
        .0
        .get_i32("iterations")
        .map_err(DocumentDBError::pg_response_invalid)?;
    let salt = doc
        .0
        .get_str("salt")
        .map_err(DocumentDBError::pg_response_invalid)?;

    Ok((salt.to_string(), iterations))
}

pub async fn get_user_oid(connection_context: &ConnectionContext, username: &str) -> Result<u32> {
    let user_oid_rows = connection_context
        .service_context
        .connection_pool_manager()
        .authentication_connection()
        .await?
        .query(
            "SELECT oid from pg_roles WHERE rolname = $1",
            &[Type::TEXT],
            &[&username],
            None,
            &mut RequestTracker::new(),
        )
        .await?;

    let user_oid = user_oid_rows
        .first()
        .ok_or(DocumentDBError::pg_response_empty())?
        .try_get::<_, tokio_postgres::types::Oid>(0)?;

    Ok(user_oid)
}
