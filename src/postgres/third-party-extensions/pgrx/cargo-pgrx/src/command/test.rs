//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use eyre::Context;
use pgrx_pg_config::{get_target_dir, PgConfig, Pgrx};
use std::path::{Path, PathBuf};
use std::process::Stdio;

use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::profile::CargoProfile;
use crate::CommandExecute;

/// Run the test suite for this crate
#[derive(clap::Args, Debug, Clone)]
#[clap(author)]
pub(crate) struct Test {
    /// Do you want to run against pg13, pg14, pg15, pg16, pg17, or all?
    #[clap(env = "PG_VERSION")]
    pg_version: Option<String>,
    /// If specified, only run tests containing this string in their names
    testname: Option<String>,
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
    /// compile for release mode (default is debug)
    #[clap(long, short)]
    release: bool,
    /// Specific profile to use (conflicts with `--release`)
    #[clap(long)]
    profile: Option<String>,
    /// Don't regenerate the schema
    #[clap(long, short)]
    no_schema: bool,
    /// Use `sudo` to initialize and run the Postgres test instance as this system user
    #[clap(long, value_name = "USER")]
    runas: Option<String>,
    /// Initialize the test database cluster here, instead of the default location.  If used with `--runas`, then it must be writable by the user
    #[clap(long, value_name = "DIR")]
    pgdata: Option<PathBuf>,
    #[clap(flatten)]
    features: clap_cargo::Features,
    #[clap(from_global, action = clap::ArgAction::Count)]
    verbose: u8,
}

impl CommandExecute for Test {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        #[tracing::instrument(level = "error", skip(me))]
        fn perform(me: Test, pgrx: &Pgrx) -> eyre::Result<()> {
            let mut features = me.features.clone();
            let (package_manifest, _package_manifest_path) =
                get_package_manifest(&me.features, me.package.as_ref(), me.manifest_path.as_ref())?;
            let (pg_config, _pg_version) = pg_config_and_version(
                pgrx,
                &package_manifest,
                me.pg_version.clone(),
                Some(&mut features),
                true,
            )?;

            let profile = CargoProfile::from_flags(
                me.profile.as_deref(),
                if me.release { CargoProfile::Release } else { CargoProfile::Dev },
            )?;

            test_extension(
                &pg_config,
                me.manifest_path.as_ref(),
                me.package.as_ref(),
                &profile,
                me.no_schema,
                &features,
                me.testname,
                me.runas,
                me.pgdata,
            )?;

            Ok(())
        }

        let (package_manifest, _) = get_package_manifest(
            &self.features,
            self.package.as_ref(),
            self.manifest_path.as_ref(),
        )?;
        let pgrx = Pgrx::from_config()?;
        if self.pg_version == Some("all".to_string()) {
            // run the tests for **all** the Postgres versions we know about
            for v in crate::manifest::all_pg_in_both_tomls(&package_manifest, &pgrx) {
                let mut versioned_test = self.clone();
                versioned_test.pg_version = Some(v?.label()?);
                perform(versioned_test, &pgrx)?;
            }

            Ok(())
        } else {
            // attempt to run the test for the Postgres version `run_test()` will figure out
            perform(self, &pgrx)
        }
    }
}

#[tracing::instrument(skip_all, fields(
    pg_version = %pg_config.version()?,
    testname =  tracing::field::Empty,
    ?profile,
))]
pub fn test_extension(
    pg_config: &PgConfig,
    user_manifest_path: Option<impl AsRef<Path>>,
    user_package: Option<&String>,
    profile: &CargoProfile,
    no_schema: bool,
    features: &clap_cargo::Features,
    testname: Option<impl AsRef<str>>,
    runas: Option<String>,
    pgdata: Option<PathBuf>,
) -> eyre::Result<()> {
    #[cfg(target_os = "windows")]
    if runas.is_some() {
        eyre::bail!("`--runas` is not supported on Windows");
    }

    if let Some(ref testname) = testname {
        tracing::Span::current().record("testname", tracing::field::display(&testname.as_ref()));
    }
    let target_dir = get_target_dir()?;

    let mut command = crate::env::cargo();

    let no_default_features_arg = features.no_default_features;
    let mut features_arg = features.features.join(" ");
    if features.features.iter().all(|f| f != "pg_test") {
        features_arg += " pg_test";
    }

    command
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .arg("test")
        .env("CARGO_TARGET_DIR", &target_dir)
        .env("PGRX_FEATURES", features_arg.clone())
        .env("PGRX_NO_DEFAULT_FEATURES", if no_default_features_arg { "true" } else { "false" })
        .env("PGRX_ALL_FEATURES", if features.all_features { "true" } else { "false" })
        .env("PGRX_BUILD_PROFILE", profile.name())
        .env("PGRX_NO_SCHEMA", if no_schema { "true" } else { "false" });

    if let Some(runas) = runas {
        command.env("CARGO_PGRX_TEST_RUNAS", runas);
    }

    if let Some(pgdata) = pgdata {
        command.env("CARGO_PGRX_TEST_PGDATA", pgdata);
    }

    if let Ok(rust_log) = std::env::var("RUST_LOG") {
        command.env("RUST_LOG", rust_log);
    }

    if !features_arg.trim().is_empty() {
        command.arg("--features");
        command.arg(&features_arg);
    }

    if no_default_features_arg {
        command.arg("--no-default-features");
    }

    if features.all_features {
        command.arg("--all-features");
    }

    command.args(profile.cargo_args());

    if let Some(user_manifest_path) = user_manifest_path {
        command.arg("--manifest-path");
        command.arg(user_manifest_path.as_ref());
    }

    if let Some(user_package) = user_package {
        command.arg("--package");
        command.arg(user_package);
    }

    if let Some(testname) = testname {
        command.arg(testname.as_ref());
    }

    eprintln!("{command:?}");

    tracing::debug!(command = ?command, "Running");
    let status = command.status().wrap_err("failed to run cargo test")?;
    tracing::trace!(status_code = %status, command = ?command, "Finished");
    if !status.success() && !status.success() {
        // We explicitly do not want to return a spantraced error here.
        std::process::exit(1)
    }

    Ok(())
}
