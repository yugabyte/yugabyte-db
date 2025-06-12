//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
pub(crate) fn cargo() -> std::process::Command {
    let cargo = std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into());
    std::process::Command::new(cargo)
}

/// Set some environment variables for use downstream (in `pgrx-test` for
/// example). Does nothing if already set.
pub(crate) fn initialize() {
    match (std::env::var_os("CARGO_PGRX"), std::env::current_exe()) {
        (None, Ok(path)) => {
            std::env::set_var("CARGO_PGRX", path);
            // TODO: Should we set `CARGO_PGRX_{CARGO,RUSTC}` to `RUSTC`/`CARGO`
            // if unset, then prefer those? The issue with `RUSTC`/`CARGO` vars
            // is that they are unset if something invokes e.g. `cargo`
            // directly... This is probably eventually something we'll need, but
            // let's wait until that happens.
        }
        (Some(_), Ok(_)) => {
            // For now I guess we should just hope they're the same.
            // Canonicalizing here's tricky and not guaranteed to behave
            // right... although we could consider calling back into ourselves
            // so something that blindly invokes `cargo-pgrx` instead of
            // `CARGO_PGRX` will do the right thing.
            //
            // In either case if we ever get to the macos-linker-shim work this
            // will have to be slightly firmed up (if `cargo-pgrx` is still going
            // to act as the linker shim.)
        }
        //  bad but not much we can do.
        (_, Err(_)) => {}
    }
}
