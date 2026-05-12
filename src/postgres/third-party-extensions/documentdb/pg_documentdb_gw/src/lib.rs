/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/lib.rs
 *
 *-------------------------------------------------------------------------
 */

pub mod auth;
pub mod bson;
pub mod configuration;
pub mod context;
pub mod error;
pub mod explain;
pub mod postgres;
pub mod processor;
pub mod protocol;
pub mod requests;
pub mod responses;
pub mod service;
pub mod shutdown_controller;
pub mod startup;
pub mod telemetry;

pub use crate::postgres::QueryCatalog;

use either::Either::{Left, Right};
use openssl::ssl::Ssl;
use socket2::TcpKeepalive;
use std::net::IpAddr;
use std::{pin::Pin, sync::Arc, time::Duration};
use tokio::{
    io::BufStream,
    net::{TcpListener, TcpStream},
};
use tokio_openssl::SslStream;
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

use crate::{
    context::{ConnectionContext, RequestContext, ServiceContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDataClient,
    protocol::header::Header,
    requests::{request_tracker::RequestTracker, Request, RequestIntervalKind},
    responses::{CommandError, Response},
    telemetry::{
        client_info::parse_client_info, error_code_to_status_code, event_id::EventId,
        TelemetryProvider,
    },
};

// TCP keepalive configuration constants
const TCP_KEEPALIVE_TIME_SECS: u64 = 180;
const TCP_KEEPALIVE_INTERVAL_SECS: u64 = 60;

// Buffer configuration constants
const STREAM_READ_BUFFER_SIZE: usize = 8 * 1024;
const STREAM_WRITE_BUFFER_SIZE: usize = 8 * 1024;

type GwStream = BufStream<SslStream<TcpStream>>;

/// Runs the DocumentDB gateway server, accepting and handling incoming connections.
///
/// This function sets up a TCP listener and SSL context, then continuously accepts
/// new connections until the cancellation token is triggered. Each connection is
/// handled in a separate async task.
///
/// # Arguments
///
/// * `service_context` - The service configuration and context
/// * `telemetry` - Optional telemetry provider for metrics and logging
/// * `token` - Cancellation token to gracefully shutdown the gateway
///
/// # Returns
///
/// Returns `Ok(())` on successful shutdown, or an error if the server fails to start
/// or encounters a fatal error during operation.
///
/// # Errors
///
/// This function will return an error if:
/// * Failed to bind to the specified address and port
/// * SSL context creation fails
/// * Any other fatal gateway initialization error occurs
pub async fn run_gateway<T>(
    service_context: ServiceContext,
    telemetry: Option<Box<dyn TelemetryProvider>>,
    token: CancellationToken,
) -> Result<()>
where
    T: PgDataClient,
{
    // TCP configuration part
    let tcp_listener = TcpListener::bind(format!(
        "{}:{}",
        if service_context.setup_configuration().use_local_host() {
            "127.0.0.1"
        } else {
            "[::]"
        },
        service_context.setup_configuration().gateway_listen_port(),
    ))
    .await?;

    // Listen for new tcp connections until token is not cancelled
    loop {
        tokio::select! {
            stream_and_address = tcp_listener.accept() => {
                let conn_service_context = service_context.clone();
                let conn_telemetry = telemetry.clone();
                tokio::spawn(async move {
                    let conn_res = handle_connection::<T>(
                        stream_and_address,
                        conn_service_context,
                        conn_telemetry,
                    )
                    .await;

                    if let Err(conn_err) = conn_res {
                        tracing::error!("Failed to accept a connection: {conn_err:?}.");
                    }
                });
            }
            () = token.cancelled() => {
                return Ok(())
            }
        }
    }
}

/// Handles a single TCP connection by setting up TLS and processing the connection.
///
/// This function configures the TCP stream with appropriate settings (no delay, keepalive),
/// establishes a TLS session using the service's SSL configuration, and then delegates
/// to the stream handler for processing MongoDB protocol messages.
///
/// # Arguments
///
/// * `stream_and_address` - Result containing the TCP stream and peer address from accept()
/// * `service_context` - Service configuration and shared state
/// * `telemetry` - Optional telemetry provider for metrics collection
///
/// # Returns
///
/// Returns `Ok(())` on successful connection handling, or an error if connection
/// setup or TLS handshake fails.
///
/// # Errors
///
/// This function will return an error if:
/// * TCP stream configuration fails (nodelay, keepalive)
/// * SSL/TLS handshake fails
/// * Connection context creation fails
/// * Stream buffering setup fails
async fn handle_connection<T>(
    stream_and_address: std::result::Result<(TcpStream, std::net::SocketAddr), std::io::Error>,
    service_context: ServiceContext,
    telemetry: Option<Box<dyn TelemetryProvider>>,
) -> Result<()>
where
    T: PgDataClient,
{
    let (tcp_stream, peer_address) = stream_and_address?;

    let connection_id = Uuid::new_v4();
    tracing::info!(
        activity_id = connection_id.to_string().as_str(),
        "Accepted new TCP connection"
    );

    // Configure TCP stream
    tcp_stream.set_nodelay(true)?;
    let tcp_keepalive = TcpKeepalive::new()
        .with_time(Duration::from_secs(TCP_KEEPALIVE_TIME_SECS))
        .with_interval(Duration::from_secs(TCP_KEEPALIVE_INTERVAL_SECS));

    socket2::SockRef::from(&tcp_stream).set_tcp_keepalive(&tcp_keepalive)?;

    // SSL configuration part
    let tls_acceptor = service_context.tls_provider().tls_acceptor();

    let ssl_session = Ssl::new(tls_acceptor.context())?;
    let mut tls_stream = SslStream::new(ssl_session, tcp_stream)?;

    if let Err(ssl_error) = SslStream::accept(Pin::new(&mut tls_stream)).await {
        tracing::error!("Failed to create TLS connection: {ssl_error:?}.");
        return Err(DocumentDBError::internal_error(format!(
            "SSL handshake failed: {ssl_error:?}."
        )));
    }

    let ip_address = match peer_address.ip() {
        IpAddr::V4(v4) => IpAddr::V4(v4),
        IpAddr::V6(v6) => {
            // If it's an IPv4-mapped IPv6 (::ffff:a.b.c.d), extract the IPv4.
            if let Some(v4) = v6.to_ipv4_mapped() {
                IpAddr::V4(v4)
            } else {
                IpAddr::V6(v6)
            }
        }
    };

    tracing::info!(
        activity_id = connection_id.to_string().as_str(),
        "TCP connection established - Connection Id {connection_id}, client IP {ip_address}"
    );

    let connection_context = ConnectionContext::new(
        service_context,
        telemetry,
        ip_address.to_string(),
        Some(tls_stream.ssl()),
        connection_id,
        "TCP".to_string(),
    );

    let buffered_stream = BufStream::with_capacity(
        STREAM_READ_BUFFER_SIZE,
        STREAM_WRITE_BUFFER_SIZE,
        tls_stream,
    );

    handle_stream::<T>(buffered_stream, connection_context).await;
    Ok(())
}

async fn handle_stream<T>(mut stream: GwStream, mut connection_context: ConnectionContext)
where
    T: PgDataClient,
{
    let connection_activity_id = connection_context.connection_id.to_string();
    let connection_activity_id_as_str = connection_activity_id.as_str();

    loop {
        match protocol::reader::read_header(&mut stream).await {
            Ok(Some(header)) => {
                let request_activity_id =
                    connection_context.generate_request_activity_id(header.request_id);

                if let Err(e) = handle_message::<T>(
                    &mut connection_context,
                    &header,
                    &mut stream,
                    &request_activity_id,
                )
                .await
                {
                    if let Err(e) = log_and_write_error(
                        &connection_context,
                        &header,
                        &e,
                        None,
                        &mut stream,
                        None,
                        &mut RequestTracker::new(),
                        &request_activity_id,
                    )
                    .await
                    {
                        tracing::error!(
                            activity_id = request_activity_id.as_str(),
                            "Couldn't reply with error {e:?}."
                        );
                        break;
                    }
                }
            }

            Ok(None) => {
                tracing::info!(
                    activity_id = connection_activity_id_as_str,
                    "Connection closed."
                );
                break;
            }

            Err(e) => {
                if let Err(e) = responses::writer::write_error_without_header(
                    &connection_context,
                    e,
                    &mut stream,
                    connection_activity_id_as_str,
                )
                .await
                {
                    tracing::warn!(
                        activity_id = connection_activity_id_as_str,
                        "Couldn't reply with error {e:?}."
                    );
                    break;
                }
            }
        }
    }
}

async fn get_response<T>(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
) -> Result<Response>
where
    T: PgDataClient,
{
    if request_context.payload.request_type().handle_with_auth() {
        let response = auth::process::<T>(connection_context, request_context).await?;
        return Ok(response);
    }

    if !*connection_context.auth_state.is_authorized().read().await {
        if *connection_context.auth_state.auth_kind() == Some(auth::AuthKind::ExternalIdentity) {
            return Err(DocumentDBError::reauthentication_required(
                "External identity token has expired.".to_string(),
            ));
        } else {
            let response = auth::process::<T>(connection_context, request_context).await?;
            return Ok(response);
        }
    }

    // Once authorized, make sure that there is a pool of pg clients for the user/password.
    connection_context.allocate_data_pool().await?;

    let service_context = Arc::clone(&connection_context.service_context);
    let data_client = T::new_authorized(&service_context, &connection_context.auth_state).await?;

    // Process the actual request
    let response =
        processor::process_request(request_context, connection_context, data_client).await?;

    Ok(response)
}

async fn handle_message<T>(
    connection_context: &mut ConnectionContext,
    header: &Header,
    stream: &mut GwStream,
    activity_id: &str,
) -> Result<()>
where
    T: PgDataClient,
{
    // Read the request message off the stream
    let mut request_tracker = RequestTracker::new();

    let handle_request_start = request_tracker.start_timer();
    let buffer_read_start = request_tracker.start_timer();
    let message = protocol::reader::read_request(header, stream).await?;

    request_tracker.record_duration(RequestIntervalKind::BufferRead, buffer_read_start);

    if connection_context
        .dynamic_configuration()
        .send_shutdown_responses()
        .await
    {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::ShutdownInProgress,
            "Graceful shutdown requested".to_string(),
        ));
    }

    let format_request_start = request_tracker.start_timer();
    let request =
        protocol::reader::parse_request(&message, &mut connection_context.requires_response)
            .await?;
    request_tracker.record_duration(RequestIntervalKind::FormatRequest, format_request_start);

    let request_info = request.extract_common()?;
    let mut request_context = RequestContext {
        activity_id,
        payload: &request,
        info: &request_info,
        tracker: &mut request_tracker,
    };

    let mut collection = String::new();
    let request_result = handle_request::<T>(
        connection_context,
        header,
        &mut request_context,
        stream,
        &mut collection,
        handle_request_start,
    )
    .await;

    // Errors in request handling are handled explicitly so that telemetry can have access to the request
    // Returns Ok afterwards so that higher level error telemetry is not invoked.
    let command_error = if let Err(e) = request_result {
        match log_and_write_error(
            connection_context,
            header,
            &e,
            Some(request_context.payload),
            stream,
            Some(collection),
            request_context.tracker,
            activity_id,
        )
        .await
        {
            Ok(command_error) => Some(command_error),
            Err(write_err) => {
                tracing::error!(
                    activity_id = activity_id,
                    "Couldn't reply with error {write_err:?}."
                );
                None
            }
        }
    } else {
        None
    };

    if connection_context
        .dynamic_configuration()
        .enable_verbose_logging_in_gateway()
        .await
    {
        log_verbose_latency(connection_context, &request_context, command_error.as_ref()).await;
    }

    Ok(())
}

async fn handle_request<T>(
    connection_context: &mut ConnectionContext,
    header: &Header,
    request_context: &mut RequestContext<'_>,
    stream: &mut GwStream,
    collection: &mut String,
    handle_request_start: tokio::time::Instant,
) -> Result<()>
where
    T: PgDataClient,
{
    *collection = request_context.info.collection().unwrap_or("").to_string();

    let format_response_start = request_context.tracker.start_timer();

    // Process the response for the message
    let response_result = get_response::<T>(request_context, connection_context).await;

    // Always record durations, regardless of success or error
    request_context
        .tracker
        .record_duration(RequestIntervalKind::FormatResponse, format_response_start);

    request_context
        .tracker
        .record_duration(RequestIntervalKind::HandleRequest, handle_request_start);

    let response = match response_result {
        Ok(response) => response,
        Err(e) => {
            return Err(e);
        }
    };

    // Write the response back to the stream
    if connection_context.requires_response {
        responses::writer::write(header, &response, stream).await?;
    }

    if let Some(telemetry) = connection_context.telemetry_provider.as_ref() {
        telemetry
            .emit_request_event(
                connection_context,
                header,
                Some(request_context.payload),
                Left(&response),
                collection.to_string(),
                request_context.tracker,
                request_context.activity_id,
                &parse_client_info(connection_context.client_information.as_ref()),
            )
            .await;
    }

    Ok(())
}

#[expect(clippy::too_many_arguments)]
async fn log_and_write_error(
    connection_context: &ConnectionContext,
    header: &Header,
    e: &DocumentDBError,
    request: Option<&Request<'_>>,
    stream: &mut GwStream,
    collection: Option<String>,
    request_tracker: &mut RequestTracker,
    activity_id: &str,
) -> Result<CommandError> {
    let command_error = CommandError::from_error(connection_context, e, activity_id).await;
    let response = command_error.to_raw_document_buf();

    responses::writer::write_and_flush(header, &response, stream).await?;

    // telemetry can block so do it after write and flush.
    tracing::error!(activity_id = activity_id, "Request failure: {e}");

    if let Some(telemetry) = connection_context.telemetry_provider.as_ref() {
        telemetry
            .emit_request_event(
                connection_context,
                header,
                request,
                Right((&command_error, response.as_bytes().len())),
                collection.unwrap_or_default(),
                request_tracker,
                activity_id,
                &parse_client_info(connection_context.client_information.as_ref()),
            )
            .await;
    }

    Ok(command_error)
}

async fn log_verbose_latency(
    connection_context: &mut ConnectionContext,
    request_context: &RequestContext<'_>,
    e: Option<&CommandError>,
) {
    let database_name = request_context.info.db().unwrap_or_default();
    let collection_name = request_context.info.collection().unwrap_or_default();
    let request_type = request_context.payload.request_type().to_string();

    let (status_code, error_code) = if let Some(error) = e {
        let code = error.code;
        (error_code_to_status_code(code), code)
    } else {
        (200, 0)
    };

    tracing::info!(
        activity_id = request_context.activity_id,
        event_id = EventId::RequestTrace.code(),
        "Latency for Mongo Request with interval timings (ns): BufferRead={}, HandleRequest={}, FormatRequest={}, ProcessRequest={}, PostgresBeginTransaction={}, PostgresSetStatementTimeout={}, PostgresTransactionCommit={}, FormatResponse={}, Address={}, TransportProtocol={}, DatabaseName={}, CollectionName={}, OperationName={}, StatusCode={}, SubStatusCode={}, ErrorCode={}",
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::BufferRead),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::HandleRequest),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::FormatRequest),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::ProcessRequest),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::PostgresBeginTransaction),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::PostgresSetStatementTimeout),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::PostgresTransactionCommit),
        request_context.tracker.get_interval_elapsed_time(RequestIntervalKind::FormatResponse),
        connection_context.ip_address,
        connection_context.transport_protocol(),
        database_name,
        collection_name,
        request_type,
        status_code,
        0, // SubStatusCode is not used currently in Rust
        error_code
    );
}
