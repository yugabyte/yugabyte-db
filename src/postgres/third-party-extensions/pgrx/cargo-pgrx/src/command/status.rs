//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use eyre::eyre;
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, PgConfigSelector, Pgrx};
use std::path::PathBuf;
use std::process::{self, Stdio};

use crate::CommandExecute;

/// Is a pgrx-managed Postgres instance running?
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Status {
    /// The Postgres version
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

impl CommandExecute for Status {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        let pgrx = Pgrx::from_config()?;

        let pg_version = match self.pg_version {
            Some(s) => s,
            None => "all".to_string(),
        };

        for pg_config in pgrx.iter(PgConfigSelector::new(&pg_version)) {
            let pg_config = pg_config?;
            if status_postgres(&pg_config)? {
                println!("Postgres v{} is {}", pg_config.major_version()?, "running".bold().green())
            } else {
                println!("Postgres v{} is {}", pg_config.major_version()?, "stopped".bold().red())
            }
        }

        Ok(())
    }
}

#[tracing::instrument(level = "error", skip_all, fields(pg_version = %pg_config.version()?))]
pub(crate) fn status_postgres(pg_config: &PgConfig) -> eyre::Result<bool> {
    let datadir = pg_config.data_dir()?;
    if let Ok(false) = datadir.try_exists() {
        // Postgres couldn't possibly be running if there's no data directory
        // and even if it were, we'd have no way of knowing
        return Ok(false);
    } // if Err, let the filesystem and OS handle our impending failure

    let pg_ctl = pg_config.pg_ctl_path()?;
    let mut command = process::Command::new(pg_ctl);
    command.stdout(Stdio::piped()).stderr(Stdio::piped()).arg("status").arg("-D").arg(&datadir);
    let command_str = format!("{command:?}");
    tracing::debug!(command = %command_str, "Running");

    let output = command.output()?;
    let code = output.status.code().unwrap();
    tracing::trace!(status_code = %code, command = %command_str, "Finished");

    let is_running = code == 0; // running
    let is_stopped = code == 3; // not running

    if !is_running && !is_stopped {
        return Err(eyre!(
            "problem running pg_ctl: {}\n\n{}",
            command_str,
            String::from_utf8(output.stderr).unwrap()
        ));
    }

    // a status code of zero means it's running
    Ok(is_running)
}
