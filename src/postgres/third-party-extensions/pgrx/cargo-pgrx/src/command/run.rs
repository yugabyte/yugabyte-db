use std::collections::HashMap;
//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::CommandExecute;
use crate::cargo::CargoProfile;
use crate::command::get::get_property;
use crate::command::install::{install_extension, warn_if_pg_bench_enabled};
use crate::command::regress::Regress;
use crate::command::start::start_postgres;
use crate::command::stop::stop_postgres;
use crate::manifest::{get_package_manifest, pg_config_and_version};
use eyre::eyre;
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, Pgrx, createdb};
use std::path::{Path, PathBuf};

/// Compile/install extension to a pgrx-managed Postgres instance and start psql
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Run {
    /// Do you want to run against pg13, pg14, pg15, pg16, pg17, or pg18?
    #[clap(env = "PG_VERSION")]
    pg_version: Option<String>,
    /// The database to connect to (and create if the first time).  Defaults to a database with the same name as the current extension name
    dbname: Option<String>,
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long)]
    manifest_path: Option<PathBuf>,
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
    #[clap(long)]
    valgrind: bool,
}

impl From<&Regress> for Run {
    fn from(regress: &Regress) -> Self {
        Self {
            pg_version: regress.pg_version.clone(),
            dbname: regress.dbname.clone(),
            package: regress.package.clone(),
            manifest_path: regress.manifest_path.clone(),
            release: regress.release,
            profile: regress.profile.clone(),
            features: regress.features.clone(),
            target: None,
            verbose: regress.verbose,
            pgcli: false,
            install_only: false,
            valgrind: regress.valgrind,
        }
    }
}

impl Run {
    pub(crate) fn install(
        &mut self,
        create_database: bool,
        postgresql_conf: &HashMap<String, String>,
    ) -> eyre::Result<(PgConfig, String)> {
        let pgrx = Pgrx::from_config()?;

        // If the first positional arg isn't a recognized PG version (pgXX)
        // and no dbname was given, treat it as a dbname
        if let Some(ref v) = self.pg_version
            && !pgrx.is_feature_flag(v)
            && self.dbname.is_none()
        {
            self.dbname = self.pg_version.take();
        }

        let (package_manifest, package_manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_deref(),
            self.manifest_path.as_deref(),
        )?;
        let (pg_config, _pg_version) = pg_config_and_version(
            &pgrx,
            &package_manifest,
            self.pg_version.clone(),
            Some(&mut self.features),
            true,
        )?;

        let dbname = match &self.dbname {
            Some(dbname) => dbname.clone(),
            None => get_property(&package_manifest_path, "extname")?
                .ok_or(eyre!("could not determine extension name"))?,
        };
        let profile = CargoProfile::from_flags(
            self.profile.as_deref(),
            if self.release { CargoProfile::Release } else { CargoProfile::Dev },
        )?;

        run(
            &pg_config,
            self.manifest_path.as_deref(),
            self.package.as_deref(),
            &package_manifest_path,
            &dbname,
            create_database,
            &profile,
            &self.features,
            self.install_only,
            self.valgrind,
            self.target.as_deref(),
            postgresql_conf,
        )?;

        Ok((pg_config, dbname))
    }
}

impl CommandExecute for Run {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(mut self) -> eyre::Result<()> {
        warn_if_pg_bench_enabled(&self.features, "run");
        let (pg_config, dbname) = self.install(true, &Default::default())?;

        // run psql
        exec_psql(&pg_config, &dbname, self.pgcli)
    }
}

#[tracing::instrument(level = "error", skip_all, fields(
    pg_version = %pg_config.version()?,
    dbname,
    profile = ?profile,
))]
pub(crate) fn run(
    pg_config: &PgConfig,
    user_manifest_path: Option<&Path>,
    user_package: Option<&str>,
    package_manifest_path: &Path,
    dbname: &str,
    create_database: bool,
    profile: &CargoProfile,
    features: &clap_cargo::Features,
    install_only: bool,
    use_valgrind: bool,
    target: Option<&str>,
    postgresql_conf: &HashMap<String, String>,
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
    start_postgres(pg_config, postgresql_conf, use_valgrind)?;

    // create the named database
    if create_database && !createdb(pg_config, dbname, false, true, None)? {
        println!("{} existing database {}", "    Re-using".bold().cyan(), dbname);
    }

    Ok(())
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
