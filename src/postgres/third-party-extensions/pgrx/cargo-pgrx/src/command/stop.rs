//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::command::status::status_postgres;
use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::CommandExecute;
use eyre::eyre;
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, Pgrx};
use std::path::PathBuf;
use std::process::Stdio;

/// Stop a pgrx-managed Postgres instance
#[derive(clap::Args, Debug, Clone)]
#[clap(author)]
pub(crate) struct Stop {
    /// The Postgres version to stop (pg13, pg14, pg15, pg16, pg17, or all)
    #[clap(env = "PG_VERSION")]
    pg_version: Option<String>,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
    /// Package to determine default `pg_version` with (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
}

impl CommandExecute for Stop {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        fn perform(me: Stop, pgrx: &Pgrx) -> eyre::Result<()> {
            let (package_manifest, _) = get_package_manifest(
                &clap_cargo::Features::default(),
                me.package.as_ref(),
                me.manifest_path.clone(),
            )?;
            let (pg_config, _) =
                pg_config_and_version(pgrx, &package_manifest, me.pg_version, None, false)?;

            stop_postgres(&pg_config)
        }

        let pgrx = Pgrx::from_config()?;
        let (package_manifest, _) = get_package_manifest(
            &clap_cargo::Features::default(),
            self.package.as_ref(),
            self.manifest_path.clone(),
        )?;
        if self.pg_version == Some("all".into()) {
            for v in crate::manifest::all_pg_in_both_tomls(&package_manifest, &pgrx) {
                let mut versioned_start = self.clone();
                versioned_start.pg_version = Some(v?.label()?);
                perform(versioned_start, &pgrx)?;
            }
            Ok(())
        } else {
            perform(self, &pgrx)
        }
    }
}

#[tracing::instrument(level = "error", skip_all, fields(pg_version = %pg_config.version()?))]
pub(crate) fn stop_postgres(pg_config: &PgConfig) -> eyre::Result<()> {
    let datadir = pg_config.data_dir()?;

    if !(status_postgres(pg_config)?) {
        // it's not running, no need to stop it
        tracing::debug!("Already stopped");
        return Ok(());
    }

    println!("{} Postgres v{}", "    Stopping".bold().green(), pg_config.major_version()?);

    let pg_ctl = pg_config.pg_ctl_path()?;
    let mut command = std::process::Command::new(pg_ctl);
    command
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("stop")
        .arg("-D")
        .arg(&datadir)
        .arg("-m")
        .arg("fast");

    let output = command.output()?;

    if !output.status.success() {
        Err(eyre!("{}", String::from_utf8(output.stderr)?))
    } else {
        Ok(())
    }
}
