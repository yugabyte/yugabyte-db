/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/service/docdb_openssl.rs
 *
 *-------------------------------------------------------------------------
 */

use std::process::{Command, Stdio};

use openssl::{
    error::ErrorStack,
    ssl::{SslAcceptor, SslAcceptorBuilder, SslMethod, SslVersion},
};

use crate::{
    error::{DocumentDBError, Result},
    service::tls::CertificateBundle,
};

/// Function type for creating custom SSL acceptor builders.
///
/// This type alias defines the signature for functions that can create
/// [`SslAcceptorBuilder`] instances with custom configurations. It allows
/// for dependency injection and testing with mock SSL acceptors.
///
/// # Parameters
///
/// * `method` - The SSL method to use for the acceptor
///
/// # Returns
///
/// Returns a configured [`SslAcceptorBuilder`] on success, or an [`ErrorStack`] on failure.
pub type AcceptorBuilderFn =
    fn(method: SslMethod) -> std::result::Result<SslAcceptorBuilder, ErrorStack>;

/// Default certificate validity period in days for auto-generated certificates
const DEFAULT_CERT_VALIDITY_DAYS: &str = "365";

/// Certificate subject for auto-generated certificates
const DEFAULT_CERT_SUBJECT: &str = "/CN=localhost";

/// Generates an RSA private key and self-signed certificate using OpenSSL commands.
///
/// This function creates a new 2048-bit RSA private key and a corresponding
/// self-signed X.509 certificate valid for 365 days. The certificate uses
/// "CN=localhost" as the subject, making it suitable for local development
/// and testing scenarios.
///
/// # Arguments
///
/// * `private_key_path` - File system path where the RSA private key will be saved in PEM format
/// * `public_key_path` - File system path where the X.509 certificate will be saved in PEM format
///
/// # Returns
///
/// Returns [`Ok(())`] on successful key and certificate generation, or a [`DocumentDBError`]
/// if any step fails.
///
/// # Errors
///
/// This function will return an error if:
/// * OpenSSL is not installed or not available in the system PATH
/// * File system permissions prevent writing to the specified paths
/// * The parent directories for the specified paths do not exist
/// * OpenSSL command execution fails due to invalid parameters or system issues
/// * Insufficient disk space for writing the certificate files
///
/// # Security Considerations
///
/// The generated certificates are self-signed and should only be used for
/// development and testing purposes. For production environments, use
/// certificates signed by a trusted Certificate Authority (CA).
pub fn generate_auth_keys(private_key_path: &str, public_key_path: &str) -> Result<()> {
    // openssl genpkey -algorithm RSA -out private_key.pem -outform PEM
    let pkey_output = Command::new("openssl")
        .arg("genpkey")
        .args(["-algorithm", "RSA"])
        .args(["-out", private_key_path, "-outform", "PEM"])
        .stdout(Stdio::null())
        .output()?;

    if !pkey_output.status.success() {
        return Err(DocumentDBError::internal_error(format!(
            "OpenSSL failed during private key generation: '{}'.",
            String::from_utf8_lossy(&pkey_output.stderr)
        )));
    }

    // openssl req -key private_key.pem -new -x509 -days 365 -out public_key.pem -outform PEM -subj "/CN=localhost"
    let pub_key_output = Command::new("openssl")
        .arg("req")
        .args(["-key", private_key_path])
        .args([
            "-new",
            "-x509",
            "-days",
            DEFAULT_CERT_VALIDITY_DAYS,
            "-out",
            public_key_path,
            "-outform",
            "PEM",
        ])
        .args(["-subj", DEFAULT_CERT_SUBJECT])
        .output()?;

    if !pub_key_output.status.success() {
        return Err(DocumentDBError::internal_error(format!(
            "OpenSSL failed during certificate generation: '{}'.",
            String::from_utf8_lossy(&pub_key_output.stderr)
        )));
    }

    Ok(())
}

/// Creates an SSL acceptor configured for the `DocumentDB` gateway service.
///
/// This function sets up an [`SslAcceptorBuilder`] with security settings appropriate
/// for handling incoming TLS connections to the `DocumentDB` gateway. It uses Mozilla's
/// intermediate TLS configuration profile to balance security and client compatibility,
/// supporting both TLS 1.2 and TLS 1.3 protocols.
///
/// The acceptor is configured with:
/// - Mozilla's intermediate v5 TLS configuration (unless custom builder provided)
/// - Minimum protocol version: TLS 1.2
/// - Maximum protocol version: TLS 1.3
/// - Server certificate and private key from the provided certificate bundle
/// - Certificate Authority chain for certificate validation
///
/// # Arguments
///
/// * `cert_bundle` - Certificate bundle containing the server certificate, CA chain, and private key
/// * `acceptor_builder` - Optional custom function for creating the SSL acceptor builder.
///   If `None`, uses [`SslAcceptor::mozilla_intermediate_v5`] for secure defaults
///
/// # Returns
///
/// Returns a configured [`SslAcceptorBuilder`] that can be further customized and
/// built into an [`SslAcceptor`] for handling TLS connections.
///
/// # Errors
///
/// This function will return an error if:
/// * The certificate bundle contains invalid or corrupted certificate data
/// * The private key format is incompatible or corrupted
/// * Certificate and private key do not match (key pair mismatch)
/// * SSL context configuration fails due to OpenSSL library issues
/// * Certificate validation fails (e.g., expired, malformed)
/// * TLS version configuration is invalid or unsupported
/// * CA certificate chain contains invalid certificates
///
/// # Security Notes
///
/// The intermediate TLS configuration provides a good balance between security
/// and compatibility. It supports modern cipher suites while maintaining
/// compatibility with older clients that may not support TLS 1.3.
///
/// For more information about Mozilla's TLS recommendations, see:
/// <https://wiki.mozilla.org/Security/Server_Side_TLS>
///
/// # Performance Considerations
///
/// TLS 1.3 provides better performance and security compared to TLS 1.2.
/// However, TLS 1.2 support is maintained for backward compatibility with
/// older client applications that may not support TLS 1.3.
pub fn create_tls_acceptor(
    cert_bundle: &CertificateBundle,
    acceptor_builder: Option<AcceptorBuilderFn>,
) -> Result<SslAcceptorBuilder> {
    let mut ssl_acceptor = match acceptor_builder {
        Some(builder) => builder(SslMethod::tls_server())?,
        None => SslAcceptor::mozilla_intermediate_v5(SslMethod::tls_server())?,
    };

    ssl_acceptor.set_private_key(cert_bundle.private_key())?;
    ssl_acceptor.set_certificate(&cert_bundle.certificate())?;

    for ca_cert in cert_bundle.ca_chain() {
        ssl_acceptor.cert_store_mut().add_cert(ca_cert.clone())?;
    }

    // SSL server settings
    ssl_acceptor.set_min_proto_version(Some(SslVersion::TLS1_2))?;
    ssl_acceptor.set_max_proto_version(Some(SslVersion::TLS1_3))?;

    Ok(ssl_acceptor)
}
