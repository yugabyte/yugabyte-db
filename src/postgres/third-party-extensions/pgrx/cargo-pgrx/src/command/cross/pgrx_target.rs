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
use eyre::{eyre, Result, WrapErr};
use pgrx_pg_config::PgConfig;
use std::{
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

/// Build and output a PGRX target bundle into the current directory.
///
/// This file is a tarball containing information which can be used to help
/// build PG extensions compatible with this machine's PostgreSQL installation.
/// It is optional, but recommended for most cases (any case where the host and
/// target are not identical versions of Debian).
///
/// See the documentation in `CROSS_COMPILE.md` in <https://github.com/pgcentralfoundation/pgrx/>
/// for specifics of this file format and how to use the resulting file. Note
/// that this is currently unlikely to be useful on non-Linux targets, as pgrx
/// does not yet support cross-compilation on those targets.
#[derive(clap::Args, Debug)]
pub(crate) struct PgrxTarget {
    /// The `pg_config` path (default is the first `pg_config` in "$PATH").
    ///
    /// Caveat: Running this against PostgreSQL installations placed in
    /// "$PGRX_HOME/$pgver/" by `cargo pgrx init` is probably a mistake in most cases.
    #[arg(long, short = 'c', value_parser)]
    pub pg_config: Option<PathBuf>,

    /// Output filename. Defaults to `pgrx-target.$target_arch.tgz`
    #[arg(long, short = 'o', value_parser)]
    pub output: Option<PathBuf>,

    /// Override the `pgrx-pg-sys` dependency (used to generate bindings). By
    /// default we use a version of pgrx-pg-sys which has the same same version
    /// as the `cargo-pgrx` binary.
    #[arg(long, value_parser)]
    pub pg_sys_path: Option<PathBuf>,

    /// The PostgreSQL major version that is needed. We will error if the
    /// provided `pg_config` is not that version.
    #[arg(long, short = 'P', value_parser)]
    pub pg_version: Option<u16>,

    /// Scratch directory to use to build the temporary crate. Defaults to a
    /// temporary directory, and shouldn't usually need to be specified. This
    /// directory should not already exist.
    #[arg(long, value_parser)]
    pub scratch_dir: Option<PathBuf>,
}

impl CommandExecute for PgrxTarget {
    fn execute(self) -> eyre::Result<()> {
        let mut temp: Option<tempfile::TempDir> = None;
        let temp_path = if let Some(scratch) = &self.scratch_dir {
            &**scratch
        } else {
            temp = Some(tempfile::tempdir()?);
            temp.as_ref().unwrap().path()
        };
        make_target_info(&self, temp_path)?;
        if let Some(temp) = temp {
            temp.close()?;
        }
        Ok(())
    }
}

#[tracing::instrument(level = "error")]
fn make_target_info(cmd: &PgrxTarget, tmp: &Path) -> Result<()> {
    let pg_config_path = cmd.pg_config.clone().unwrap_or_else(|| "pg_config".into()).to_owned();
    let pg_config = PgConfig::new_with_defaults(pg_config_path.clone());

    let major_version = pg_config.major_version()?;
    if let Some(expected_pg_version) = cmd.pg_version {
        eyre::ensure!(
            major_version == expected_pg_version,
            "the provided `pg_config` had the wrong major version",
        );
    }

    run(crate::env::cargo().args(["init", "--lib", "--name", "temp-crate"]).current_dir(tmp))?;

    let cargo_add: Vec<String> = if let Some(pg_sys_path) = &cmd.pg_sys_path {
        let abs = pg_sys_path.canonicalize().wrap_err_with(|| {
            format!("given `--pg-sys-path` could not be canonicalized: {pg_sys_path:?}")
        })?;
        vec!["pgrx-pg-sys".into(), "--path".into(), abs.display().to_string()]
    } else {
        let own_version = env!("CARGO_PKG_VERSION");
        vec![format!("pgrx-pg-sys@={own_version}")]
    };

    run(crate::env::cargo()
        .arg("add")
        .args(cargo_add)
        .arg("--no-default-features")
        .current_dir(tmp))?;

    let filename = format!("pg{major_version}_raw_bindings.rs");
    run(crate::env::cargo()
        .current_dir(tmp)
        .arg("build")
        .arg("--features")
        .arg(format!("pgrx-pg-sys/pg{major_version}"))
        .env("PGRX_PG_CONFIG_PATH", &pg_config_path)
        .env("PGRX_PG_SYS_EXTRA_OUTPUT_PATH", tmp.join(&filename)))?;

    run(Command::new("rustfmt").current_dir(tmp).arg(&filename))?;
    run(Command::new("tar").current_dir(tmp).arg("czf").arg("out.tgz").arg(&filename))?;
    std::fs::rename(tmp.join("out.tgz"), format!("pgrx-target.{}.tgz", std::env::consts::ARCH))?;

    Ok(())
}

#[tracing::instrument(level = "info", fields(command = ?c), err)]
fn run(c: &mut Command) -> Result<()> {
    c.stdout(Stdio::inherit()).stderr(Stdio::inherit());
    let status = c.status().wrap_err("Unable to create temporary crate")?;
    if !status.success() {
        Err(eyre!("{c:?} failed with exit code: {status}"))
    } else {
        Ok(())
    }
}
