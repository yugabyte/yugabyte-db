//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    if let Some(cargo_version) = cargo_version() {
        println!("cargo:rustc-env=CARGO_VERSION_DURING_BUILD={cargo_version}");
    }
}

/// Gets the current `cargo` version.
///
/// Used to check our toolchain version. `cargo` sets the `CARGO` env var both
/// when running the build script and when running `cargo-pgrx`, so it's easier
/// to check `cargo` than to check `rustc` itself. Also `cargo` will
/// always have the same version as the `rustc` it goes with, so checking the
/// `cargo` version is sufficient.
///
/// [Reference: Environment variables Cargo sets for build
/// scripts](https://doc.rust-lang.org/cargo/reference/environment-variables.html#environment-variables-cargo-sets-for-build-scripts)
fn cargo_version() -> Option<String> {
    let cargo = std::env::var_os("CARGO").expect("`CARGO` env var wasn't set!");
    let output = std::process::Command::new(cargo).arg("--version").output().ok()?;
    std::str::from_utf8(&output.stdout).map(|s| s.trim().to_string()).ok()
}
