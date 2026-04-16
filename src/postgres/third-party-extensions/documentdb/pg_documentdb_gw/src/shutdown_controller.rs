/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/shutdown_controller.rs
 *
 *-------------------------------------------------------------------------
 */

use once_cell::sync::Lazy;
use tokio_util::sync::CancellationToken;

pub struct ShutdownController {
    token: CancellationToken,
}

// Global singleton
pub static SHUTDOWN_CONTROLLER: Lazy<ShutdownController> = Lazy::new(|| ShutdownController {
    token: CancellationToken::new(),
});

impl ShutdownController {
    pub fn token(&self) -> CancellationToken {
        self.token.clone()
    }

    pub fn shutdown(&self) {
        self.token.cancel();
    }
}
