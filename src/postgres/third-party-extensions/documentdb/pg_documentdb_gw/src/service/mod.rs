/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/service/mod.rs
 *
 *-------------------------------------------------------------------------
 */

mod docdb_openssl;
mod tls;

pub use tls::{create_tls_acceptor_builder, TlsProvider};
