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
use crate::command::run::exec_psql;
use crate::command::start::start_postgres;
use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::CommandExecute;
use clap_cargo::Features;
use eyre::{eyre, WrapErr};
use owo_colors::OwoColorize;
use pgrx_pg_config::{createdb, PgConfig, Pgrx};
use std::path::PathBuf;

/// Connect, via psql, to a Postgres instance
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Connect {
    /// Do you want to run against pg13, pg14, pg15, pg16, or pg17?
    #[clap(env = "PG_VERSION")]
    pg_version: Option<String>,
    /// The database to connect to (and create if the first time).  Defaults to a database with the same name as the current extension name
    #[clap(env = "DBNAME")]
    dbname: Option<String>,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
    /// Package to determine default `pg_version` with (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
    /// Use an existing `pgcli` on the $PATH.
    #[clap(env = "PGRX_PGCLI", long)]
    pgcli: bool,
}

impl CommandExecute for Connect {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(mut self) -> eyre::Result<()> {
        let pgrx = Pgrx::from_config()?;

        let (package_manifest, package_manifest_path) = get_package_manifest(
            &Features::default(),
            self.package.as_ref(),
            self.manifest_path.as_ref(),
        )?;
        let (pg_config, _pg_version) = match pg_config_and_version(
            &pgrx,
            &package_manifest,
            self.pg_version.clone(),
            None,
            true,
        ) {
            Ok(values) => values,
            Err(_) => {
                // the pg_version was likely a database name
                self.dbname = self.pg_version.clone();

                // try again, this time failing if necessary
                self.pg_version = None;
                pg_config_and_version(&pgrx, &package_manifest, self.pg_version, None, true)?
            }
        };

        let dbname = match self.dbname {
            Some(dbname) => dbname,
            None => {
                // We should infer from package
                get_property(package_manifest_path, "extname")
                    .wrap_err("could not determine extension name")?
                    .ok_or(eyre!("extname not found in control file"))?
            }
        };

        connect_psql(&pg_config, &dbname, self.pgcli)
    }
}

#[tracing::instrument(level = "error", skip_all, fields(
    pg_version = %pg_config.version()?,
    dbname,
))]
pub(crate) fn connect_psql(pg_config: &PgConfig, dbname: &str, pgcli: bool) -> eyre::Result<()> {
    // restart postgres
    start_postgres(pg_config)?;

    // create the named database
    if !createdb(pg_config, dbname, false, true, None)? {
        println!("{} existing database {}", "    Re-using".bold().cyan(), dbname);
    }

    // run psql
    exec_psql(pg_config, dbname, pgcli)
}
