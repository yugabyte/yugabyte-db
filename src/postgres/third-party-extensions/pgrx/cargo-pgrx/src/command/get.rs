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
use eyre::{eyre, WrapErr};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::Command;

/// Get a property from the extension control file
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Get {
    /// One of the properties from `$EXTENSION.control`
    name: String,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
    /// Package to determine default `pg_version` with (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
}

impl CommandExecute for Get {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        let metadata = crate::metadata::metadata(&Default::default(), self.manifest_path.as_ref())
            .wrap_err("couldn't get cargo metadata")?;
        crate::metadata::validate(self.manifest_path.as_ref(), &metadata)?;
        let package_manifest_path =
            crate::manifest::manifest_path(&metadata, self.package.as_ref())
                .wrap_err("Couldn't get manifest path")?;

        if let Some(value) = get_property(package_manifest_path, &self.name)? {
            println!("{value}");
        }
        Ok(())
    }
}

#[tracing::instrument(level = "error", skip_all, fields(
    %name,
    manifest_path = %manifest_path.as_ref().display(),
))]
pub fn get_property(manifest_path: impl AsRef<Path>, name: &str) -> eyre::Result<Option<String>> {
    let (control_file, extname) = find_control_file(manifest_path)?;

    if name == "extname" {
        return Ok(Some(extname));
    } else if name == "git_hash" {
        return determine_git_hash();
    }

    let control_file = File::open(&control_file)
        .wrap_err_with(|| eyre!("could not find control file `{}`", control_file.display()))?;
    let reader = BufReader::new(control_file);

    for line in reader.lines() {
        let line = line.unwrap();
        let parts: Vec<&str> = line.split('=').collect();

        if parts.len() != 2 {
            continue;
        }

        let (k, v) = (parts.first().unwrap().trim(), parts.get(1).unwrap().trim());

        if k == name {
            let v = v.trim_start_matches('\'');
            let v = v.trim_end_matches('\'');
            return Ok(Some(v.trim().to_string()));
        }
    }

    Ok(None)
}

pub(crate) fn find_control_file(
    manifest_path: impl AsRef<Path>,
) -> eyre::Result<(PathBuf, String)> {
    let parent = manifest_path
        .as_ref()
        .parent()
        .ok_or_else(|| eyre!("could not get parent of `{}`", manifest_path.as_ref().display()))?;

    for f in std::fs::read_dir(parent).wrap_err_with(|| {
        eyre!("cannot open current directory `{}` for reading", parent.display())
    })? {
        if f.is_ok() {
            if let Ok(f) = f {
                let f_path = f.path();
                if f_path.extension() == Some("control".as_ref()) {
                    let file_stem = f_path.file_stem().ok_or_else(|| {
                        eyre!("could not get file stem of `{}`", f_path.display())
                    })?;
                    let file_stem = file_stem
                        .to_str()
                        .ok_or_else(|| {
                            eyre!("could not get file stem as String from `{}`", f_path.display())
                        })?
                        .to_string();
                    return Ok((f_path, file_stem));
                }
            }
        }
    }

    Err(eyre!("control file not found in `{}`", manifest_path.as_ref().display()))
}

fn determine_git_hash() -> eyre::Result<Option<String>> {
    match Command::new("git").arg("rev-parse").arg("HEAD").output() {
        Ok(output) => {
            if !output.status.success() {
                let stderr = String::from_utf8(output.stderr)
                    .expect("`git rev-parse head` did not return valid utf8");
                return Err(eyre!(
                    "problem running `git` to determine the current revision hash: {stderr}"
                ));
            }

            Ok(Some(
                String::from_utf8(output.stdout)
                    .expect("`git rev-parse head` did not return valid utf8")
                    .trim()
                    .into(),
            ))
        }
        Err(e) => Err(e).wrap_err("problem running `git` to determine the current revision hash"),
    }
}
