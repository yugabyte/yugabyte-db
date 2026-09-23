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
use crate::command::init::initdb;
use crate::command::status::status_postgres;
use crate::manifest::{get_package_manifest, pg_config_and_version};
use eyre::eyre;
use owo_colors::OwoColorize;
use pgrx_pg_config::{PgConfig, Pgrx};
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use std::process::Stdio;

/// Start a pgrx-managed Postgres instance
#[derive(clap::Args, Debug, Clone)]
#[clap(author)]
pub(crate) struct Start {
    /// The Postgres version to stop (pg13, pg14, pg15, pg16, pg17, pg18, or all)
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

    #[clap(long)]
    postgresql_conf: Vec<String>,
    #[clap(long)]
    valgrind: bool,
}

impl CommandExecute for Start {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        fn perform(
            me: Start,
            pgrx: &Pgrx,
            postgresql_conf: &HashMap<String, String>,
        ) -> eyre::Result<()> {
            let (package_manifest, _) = get_package_manifest(
                &clap_cargo::Features::default(),
                me.package.as_deref(),
                me.manifest_path.as_deref(),
            )?;

            let (pg_config, _) =
                pg_config_and_version(pgrx, &package_manifest, me.pg_version, None, false)?;

            start_postgres(&pg_config, postgresql_conf, me.valgrind)
        }
        let (package_manifest, _) = get_package_manifest(
            &clap_cargo::Features::default(),
            self.package.as_deref(),
            self.manifest_path.as_deref(),
        )?;

        let postgresql_conf = collect_postgresql_conf_settings(&self.postgresql_conf)?;
        let pgrx = Pgrx::from_config()?;
        if self.pg_version == Some("all".into()) {
            for v in crate::manifest::all_pg_in_both_tomls(&package_manifest, &pgrx) {
                let mut versioned_start = self.clone();
                versioned_start.pg_version = Some(v?.label()?);
                perform(versioned_start, &pgrx, &postgresql_conf)?;
            }
            Ok(())
        } else {
            perform(self, &pgrx, &postgresql_conf)
        }
    }
}

pub(crate) fn collect_postgresql_conf_settings(
    settings: &[String],
) -> eyre::Result<HashMap<String, String>> {
    settings
        .iter()
        .map(|setting| {
            if let Some((key, value)) = setting.split_once('=') {
                Ok((key.to_string(), value.to_string()))
            } else {
                Err(eyre::eyre!(
                    "`--postgresql_conf` setting of `{}` is not in the correct format",
                    setting
                ))
            }
        })
        .collect()
}

#[tracing::instrument(level = "error", skip_all, fields(pg_version = %pg_config.version()?))]
pub(crate) fn start_postgres(
    pg_config: &PgConfig,
    runtime_conf: &HashMap<String, String>,
    use_valgrind: bool,
) -> eyre::Result<()> {
    let datadir = pg_config.data_dir()?;
    let logfile = pg_config.log_file()?;
    let bindir = pg_config.bin_dir()?;
    let port = pg_config.port()?;

    if !datadir.try_exists()? {
        initdb(&bindir, &datadir)?;
    }

    if status_postgres(pg_config)? {
        tracing::debug!("Already started");
        return Ok(());
    }

    println!(
        "{} Postgres v{} on port {}",
        "    Starting".bold().green(),
        pg_config.major_version()?,
        port.to_string().bold().cyan()
    );
    let pg_ctl = pg_config.pg_ctl_path()?;

    let postmaster_path = if use_valgrind {
        #[allow(unused_mut)]
        let mut builder = tempfile::Builder::new();
        #[cfg(target_family = "unix")]
        {
            use std::os::unix::fs::PermissionsExt;
            let permission = std::fs::Permissions::from_mode(0o700);
            builder.permissions(permission);
        }
        let mut file = builder.tempfile()?;
        file.write_all(b"#!/usr/bin/sh\n")?;
        let mut command = Command::new("exec");
        command.arg("valgrind");
        command.arg("--leak-check=no");
        command.arg("--gen-suppressions=all");
        command.arg("--time-stamp=yes");
        command.arg("--error-markers=VALGRINDERROR-BEGIN,VALGRINDERROR-END");
        command.arg("--trace-children=yes");
        if let Ok(path) = valgrind_suppressions_path(pg_config)
            && let Ok(true) = std::fs::exists(&path)
        {
            command.arg(format!("--suppressions={}", path.display()));
        }
        command.arg(pg_config.postmaster_path()?.display().to_string());
        file.write_all(format!("{command:?}").as_bytes())?;
        file.write_all(b" \"$@\"\n")?;
        Some(file.into_temp_path())
    } else {
        None
    };

    let mut postmaster_args = vec![
        "-i".into(), // listen for tcp connections
        "-p".into(), // on this port
        port.to_string(),
        "-c".into(), // and allow unix socket connections
        format!("unix_socket_directories={}", Pgrx::home()?.display()),
    ];

    // user-provided settings to set/override what's in their existing `postgresql.conf`
    for (key, value) in runtime_conf {
        postmaster_args.push("-c".to_string());
        postmaster_args.push(format!("{key}={value}"));
    }

    let mut command = Command::new(pg_ctl);
    command
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("start")
        .arg("--core-files")
        .arg("-o")
        .arg(postmaster_args.join(" "))
        .arg("-D")
        .arg(&datadir)
        .arg("-l")
        .arg(&logfile);
    if let Some(postmaster_path) = postmaster_path.as_ref() {
        command.arg("-p").arg(postmaster_path);
    }
    #[cfg(target_os = "windows")]
    {
        // on windows, created pipes are leaked, so that the command hangs
        command.stdout(Stdio::inherit()).stderr(Stdio::inherit());
    }

    let command_str = format!("{command:?}");
    tracing::debug!(command = %command_str, "Running");
    let output = command.output()?;
    tracing::trace!(status_code = %output.status, command = %command_str, "Finished");
    if !output.status.success() {
        return Err(eyre!(
            "problem running pg_ctl: {}\n\n{}",
            command_str,
            String::from_utf8(output.stderr).unwrap()
        ));
    }

    Ok(())
}

fn valgrind_suppressions_path(pg_config: &PgConfig) -> Result<PathBuf, eyre::Report> {
    let mut home = Pgrx::home()?;
    home.push(pg_config.version()?);
    home.push("src/tools/valgrind.supp");
    Ok(home)
}
