//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::command::get::get_property;
use crate::command::install::install_extension;
use crate::command::start::start_postgres;
use crate::command::stop::stop_postgres;
use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::profile::CargoProfile;
use crate::CommandExecute;
use eyre::eyre;
use owo_colors::OwoColorize;
use pgrx_pg_config::{createdb, PgConfig, Pgrx};
use std::path::Path;

/// Compile/install extension to a pgrx-managed Postgres instance and start psql
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Run {
    /// Do you want to run against pg13, pg14, pg15, pg16, or pg17?
    #[clap(env = "PG_VERSION")]
    pg_version: Option<String>,
    /// The database to connect to (and create if the first time).  Defaults to a database with the same name as the current extension name
    dbname: Option<String>,
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long)]
    manifest_path: Option<String>,
    /// Compile for release mode (default is debug)
    #[clap(long, short)]
    release: bool,
    /// Specific profile to use (conflicts with `--release`)
    #[clap(long)]
    profile: Option<String>,
    #[clap(flatten)]
    features: clap_cargo::Features,
    #[clap(long)]
    target: Option<String>,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
    /// Use an existing `pgcli` on the $PATH.
    #[clap(env = "PGRX_PGCLI", long)]
    pgcli: bool,
    /// Install without running
    #[clap(long)]
    install_only: bool,
}

impl CommandExecute for Run {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(mut self) -> eyre::Result<()> {
        let pgrx = Pgrx::from_config()?;
        let (package_manifest, package_manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_ref(),
            self.manifest_path.as_ref(),
        )?;
        let (pg_config, _pg_version) = pg_config_and_version(
            &pgrx,
            &package_manifest,
            self.pg_version.clone(),
            Some(&mut self.features),
            true,
        )?;

        let dbname = match self.dbname {
            Some(dbname) => dbname,
            None => get_property(&package_manifest_path, "extname")?
                .ok_or(eyre!("could not determine extension name"))?,
        };
        let profile = CargoProfile::from_flags(
            self.profile.as_deref(),
            if self.release { CargoProfile::Release } else { CargoProfile::Dev },
        )?;

        run(
            &pg_config,
            self.manifest_path.as_ref(),
            self.package.as_ref(),
            &package_manifest_path,
            &dbname,
            &profile,
            self.pgcli,
            &self.features,
            self.install_only,
            self.target.as_ref().map(|x| x.as_str()),
        )
    }
}

#[tracing::instrument(level = "error", skip_all, fields(
    pg_version = %pg_config.version()?,
    dbname,
    profile = ?profile,
))]
pub(crate) fn run(
    pg_config: &PgConfig,
    user_manifest_path: Option<impl AsRef<Path>>,
    user_package: Option<&String>,
    package_manifest_path: &Path,
    dbname: &str,
    profile: &CargoProfile,
    pgcli: bool,
    features: &clap_cargo::Features,
    install_only: bool,
    target: Option<&str>,
) -> eyre::Result<()> {
    // stop postgres
    stop_postgres(pg_config)?;

    // install the extension
    install_extension(
        user_manifest_path,
        user_package,
        package_manifest_path,
        pg_config,
        profile,
        false,
        None,
        features,
        target,
    )?;

    if install_only {
        return Ok(());
    }

    // restart postgres
    start_postgres(pg_config)?;

    // create the named database
    if !createdb(pg_config, dbname, false, true, None)? {
        println!("{} existing database {}", "    Re-using".bold().cyan(), dbname);
    }

    // run psql
    exec_psql(pg_config, dbname, pgcli)
}

#[cfg(unix)]
pub(crate) fn exec_psql(pg_config: &PgConfig, dbname: &str, pgcli: bool) -> eyre::Result<()> {
    use std::os::unix::process::CommandExt;
    use std::process::Command;
    let mut command = Command::new(match pgcli {
        false => pg_config.psql_path()?.into_os_string(),
        true => "pgcli".to_string().into(),
    });
    command
        .env_remove("PGDATABASE")
        .env_remove("PGHOST")
        .env_remove("PGPORT")
        .env_remove("PGUSER")
        .arg("-h")
        .arg(pg_config.host())
        .arg("-p")
        .arg(pg_config.port()?.to_string())
        .arg(dbname);

    // we'll never return from here as we've now become psql
    panic!("{}", command.exec());
}

#[cfg(not(unix))]
pub(crate) fn exec_psql(pg_config: &PgConfig, dbname: &str, pgcli: bool) -> eyre::Result<()> {
    use std::process::Command;
    use std::process::Stdio;
    let mut command = Command::new(match pgcli {
        false => pg_config.psql_path()?.into_os_string(),
        true => "pgcli".to_string().into(),
    });
    command
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .env_remove("PGDATABASE")
        .env_remove("PGHOST")
        .env_remove("PGPORT")
        .env_remove("PGUSER")
        .arg("-h")
        .arg(pg_config.host())
        .arg("-p")
        .arg(pg_config.port()?.to_string())
        .arg(dbname);
    let command_str = format!("{command:?}");
    tracing::debug!(command = %command_str, "Running");
    let output = command.output()?;
    tracing::trace!(status_code = %output.status, command = %command_str, "Finished");
    Ok(())
}
