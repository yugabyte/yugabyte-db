/** Manually-constructed bindings to libpq

Because pgrx is for extensions which run in the Postgres server, it rarely needs access to libpq.
However, some server-side extensions need to interact with the reality that clients exist.
Unfortunately, doing that means acknowledging that clients need authentication and authorization,
areas of concern that are far beyond what pgrx wants to involve itself with or be responsible for.

We define some types and signatures here to allow a minimal amount of usage of items from libpq,
while largely rejecting the notion that we should involve ourselves in security-laden concerns.

*/

pub mod be {

    unsafe extern "C" {
        pub static mut MyProcPort: *mut Port;
    }

    /// #define SCRAM_MAX_KEY_LEN          PG_SHA256_DIGEST_LENGTH
    /// #define PG_SHA256_DIGEST_LENGTH    32
    #[cfg(feature = "pg18")]
    const SCRAM_MAX_KEY_LEN: usize = 32;

    /// Port for Postgres 13..=16
    #[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15", feature = "pg16"))]
    #[repr(C)]
    pub struct Port {
        pub sock: crate::pgsocket,
        pub noblock: bool,
        pub proto: crate::ProtocolVersion,
        pub laddr: crate::SockAddr,
        pub raddr: crate::SockAddr,
        pub remote_host: *mut core::ffi::c_char,
        pub remote_hostname: *mut core::ffi::c_char,
        pub remote_hostname_resolv: core::ffi::c_int,
        pub remote_hostname_errcode: core::ffi::c_int,
        pub remote_port: *mut core::ffi::c_char,
        pub canAcceptConnections: core::ffi::c_uint,
        pub database_name: *mut core::ffi::c_char,
        pub user_name: *mut core::ffi::c_char,
        pub cmdline_options: *mut core::ffi::c_char,
        pub guc_options: *mut crate::List,
        pub application_name: *mut core::ffi::c_char,

        // The remainder is for completeness, so Rust sees Port's layout as correctly as possible.
        // Ideally we would use `extern type` so the remainder of this was seen as of unknown size.
        // An alternative is to simply treat them as private fields, so we do.

        // This should be `*mut crate::HbaLine` if we ever bind that
        hba: *mut core::ffi::c_void,

        #[cfg(any(feature = "pg14", feature = "pg15"))]
        authn_id: *const core::ffi::c_char,

        default_keepalives_idle: core::ffi::c_int,
        default_keepalives_interval: core::ffi::c_int,
        default_keepalives_count: core::ffi::c_int,
        default_tcp_user_timeout: core::ffi::c_int,
        keepalives_idle: core::ffi::c_int,
        keepalives_interval: core::ffi::c_int,
        keepalives_count: core::ffi::c_int,
        tcp_user_timeout: core::ffi::c_int,

        // as if ENABLE_GSS == false && ENABLE_SSPI == false
        gss: *mut core::ffi::c_void,

        ssl_in_use: bool,
        peer_cn: *mut core::ffi::c_char,
        #[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16"))]
        peer_dn: *mut core::ffi::c_char,
        peer_cert_valid: bool,
    }

    /// Port for Postgres 17
    #[cfg(feature = "pg17")]
    #[repr(C)]
    pub struct Port {
        pub sock: crate::pgsocket,
        pub noblock: bool,
        pub proto: crate::ProtocolVersion,
        pub laddr: crate::SockAddr,
        pub raddr: crate::SockAddr,
        pub remote_host: *mut core::ffi::c_char,
        pub remote_hostname: *mut core::ffi::c_char,
        pub remote_hostname_resolv: core::ffi::c_int,
        pub remote_hostname_errcode: core::ffi::c_int,
        pub remote_port: *mut core::ffi::c_char,
        pub canAcceptConnections: core::ffi::c_uint,
        pub database_name: *mut core::ffi::c_char,
        pub user_name: *mut core::ffi::c_char,
        pub cmdline_options: *mut core::ffi::c_char,
        pub guc_options: *mut crate::List,
        pub application_name: *mut core::ffi::c_char,

        // The remainder is for completeness, so Rust sees Port's layout as correctly as possible.
        // Ideally we would use `extern type` so the remainder of this was seen as of unknown size.
        // An alternative is to simply treat them as private fields, so we do.

        // This should be `*mut crate::HbaLine` if we ever bind that
        hba: *mut core::ffi::c_void,

        default_keepalives_idle: core::ffi::c_int,
        default_keepalives_interval: core::ffi::c_int,
        default_keepalives_count: core::ffi::c_int,
        default_tcp_user_timeout: core::ffi::c_int,
        keepalives_idle: core::ffi::c_int,
        keepalives_interval: core::ffi::c_int,
        keepalives_count: core::ffi::c_int,
        tcp_user_timeout: core::ffi::c_int,

        // as if ENABLE_GSS == false && ENABLE_SSPI == false
        gss: *mut core::ffi::c_void,

        ssl_in_use: bool,
        peer_cn: *mut core::ffi::c_char,
        peer_dn: *mut core::ffi::c_char,
        peer_cert_valid: bool,

        alpn_used: bool,

        // NOTE: 5 fields remain on PG17, but two are `#ifdef USE_OPENSSL` in Postgres 17,
        // which is complicated to correctly compile due to needing to implement `cfg(accessible)`
        #[cfg(false)]
        ssl: *mut core::ffi::c_void,
        #[cfg(false)]
        peer: *mut core::ffi::c_void,

        #[deprecated(
            since = "0.17.0",
            note = "may be incorrect on Postgres 17 depending on build `#define`s"
        )]
        raw_buf: *mut core::ffi::c_char,
        #[deprecated(
            since = "0.17.0",
            note = "may be incorrect to access on Postgres 17 depending on build `#define`s"
        )]
        raw_buf_consumed: isize,
        #[deprecated(
            since = "0.17.0",
            note = "may be incorrect to access on Postgres 17 depending on build `#define`s"
        )]
        raw_buf_remaining: isize,
    }

    /// Port for Postgres 18..
    #[cfg(feature = "pg18")]
    #[repr(C)]
    pub struct Port {
        pub sock: crate::pgsocket,
        pub noblock: bool,
        pub proto: crate::ProtocolVersion,
        pub laddr: crate::SockAddr,
        pub raddr: crate::SockAddr,
        pub remote_host: *mut core::ffi::c_char,
        pub remote_hostname: *mut core::ffi::c_char,
        pub remote_hostname_resolv: core::ffi::c_int,
        pub remote_hostname_errcode: core::ffi::c_int,
        pub remote_port: *mut core::ffi::c_char,
        pub local_host: [core::ffi::c_char; 64],
        pub database_name: *mut core::ffi::c_char,
        pub user_name: *mut core::ffi::c_char,
        pub cmdline_options: *mut core::ffi::c_char,
        pub guc_options: *mut crate::List,
        pub application_name: *mut core::ffi::c_char,

        // The remainder is for completeness, so Rust sees Port's layout as correctly as possible.
        // Ideally we would use `extern type` so the remainder of this was seen as of unknown size.
        // An alternative is to simply treat them as private fields, so we do.

        // This should be `*mut crate::HbaLine` if we ever bind that
        hba: *mut core::ffi::c_void,

        default_keepalives_idle: core::ffi::c_int,
        default_keepalives_interval: core::ffi::c_int,
        default_keepalives_count: core::ffi::c_int,
        default_tcp_user_timeout: core::ffi::c_int,
        keepalives_idle: core::ffi::c_int,
        keepalives_interval: core::ffi::c_int,
        keepalives_count: core::ffi::c_int,
        tcp_user_timeout: core::ffi::c_int,

        scram_ClientKey: [u8; SCRAM_MAX_KEY_LEN],
        scram_ServerKey: [u8; SCRAM_MAX_KEY_LEN],
        has_scram_keys: bool,

        // as if ENABLE_GSS == false && ENABLE_SSPI == false
        gss: *mut core::ffi::c_void,

        ssl_in_use: bool,
        peer_cn: *mut core::ffi::c_char,
        peer_dn: *mut core::ffi::c_char,
        peer_cert_valid: bool,

        alpn_used: bool,
        last_read_was_eof: bool,

        // as if USE_OPENSSL == false
        ssl: *mut core::ffi::c_void,
        peer: *mut core::ffi::c_void,

        raw_buf: *mut core::ffi::c_char,
        raw_buf_consumed: isize,
        raw_buf_remaining: isize,
    }
}

// The ClientAuthentication Hook cannot be implemented simply with bindgen
/// Hook type to get control in ClientAuthentication()
pub type ClientAuthentication_hook_type =
    Option<unsafe extern "C-unwind" fn(port: *mut be::Port, status: i32)>;

#[pgrx_macros::pg_guard]
unsafe extern "C-unwind" {
    pub static mut ClientAuthentication_hook: ClientAuthentication_hook_type;
}
