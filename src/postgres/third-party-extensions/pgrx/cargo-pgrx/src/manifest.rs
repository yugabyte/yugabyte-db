//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use cargo_metadata::Metadata;
use cargo_toml::Manifest;
use clap_cargo::Features;
use eyre::{Context, eyre};
use pgrx_pg_config::{PgConfig, PgConfigSelector, Pgrx};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub(crate) enum PgVersionSource {
    CliArgument(String),
    FeatureFlag(String),
    DefaultFeature(String),
    PgConfig(String),
}

impl From<PgVersionSource> for String {
    fn from(v: PgVersionSource) -> Self {
        match v {
            PgVersionSource::CliArgument(s) => s,
            PgVersionSource::FeatureFlag(s) => s,
            PgVersionSource::DefaultFeature(s) => s,
            PgVersionSource::PgConfig(s) => s,
        }
    }
}

impl PgVersionSource {
    fn label(&self) -> &String {
        match self {
            Self::CliArgument(s) => s,
            Self::FeatureFlag(s) => s,
            Self::DefaultFeature(s) => s,
            Self::PgConfig(s) => s,
        }
    }
}

#[tracing::instrument(skip_all)]
pub(crate) fn manifest_path(
    metadata: &Metadata,
    package_name: Option<&str>,
) -> eyre::Result<PathBuf> {
    let manifest_path = if let Some(package_name) = package_name {
        let found = metadata
            .packages
            .iter()
            .find(|v| v.name == *package_name)
            .ok_or_else(|| eyre!("Could not find package `{package_name}`"))?;
        tracing::debug!(manifest_path = %found.manifest_path, "Found workspace package");
        found.manifest_path.clone().into_std_path_buf()
    } else if let Some(root) = metadata.root_package() {
        tracing::debug!(manifest_path = %root.manifest_path, "Found root package");
        root.manifest_path.clone().into_std_path_buf()
    } else {
        // No root package — this is a virtual workspace. Try to auto-detect
        // the single pgrx extension crate among workspace members.
        let pgrx_members: Vec<_> = metadata
            .workspace_packages()
            .into_iter()
            .filter(|pkg| {
                // A pgrx extension is a cdylib that depends on pgrx
                let has_pgrx_dep = pkg.dependencies.iter().any(|dep| dep.name == "pgrx");
                let is_cdylib = pkg
                    .targets
                    .iter()
                    .any(|target| target.crate_types.iter().any(|ct| ct == "cdylib"));
                has_pgrx_dep && is_cdylib
            })
            .collect();

        match pgrx_members.len() {
            0 => {
                return Err(eyre!(
                    "No pgrx extension crate found in this workspace.\n\
                     Use `--package <name>` to specify the package."
                ));
            }
            1 => {
                let pkg = pgrx_members[0];
                use owo_colors::OwoColorize;
                eprintln!(
                    "{} pgrx extension crate: {} ({})",
                    "Auto-detected".bold().green(),
                    pkg.name.bold().white(),
                    pkg.manifest_path.as_std_path().display().cyan(),
                );
                tracing::debug!(manifest_path = %pkg.manifest_path, "Auto-detected pgrx extension");
                pkg.manifest_path.clone().into_std_path_buf()
            }
            n => {
                let names: Vec<_> = pgrx_members.iter().map(|p| p.name.as_str()).collect();
                return Err(eyre!(
                    "Found {} pgrx extension crates in this workspace: {}.\n\
                     Use `--package <name>` to select one.",
                    n,
                    names.join(", "),
                ));
            }
        }
    };
    Ok(manifest_path)
}

pub(crate) fn modify_features_for_version(
    pgrx: &Pgrx,
    features: Option<&mut Features>,
    manifest: &Manifest,
    pg_version: &PgVersionSource,
    test: bool,
) {
    if let Some(features) = features {
        if let Some(default_features) = manifest.features.get("default")
            && !features.no_default_features
        {
            // if the user didn't specify `--no-default-features`, which would otherwise indicate
            // they think they know what they're doing, we need to build an explicit set of features
            // to use and turn on `--no-default-features`

            features.no_default_features = true;
            features.features.extend(
                default_features
                    .iter()
                    // only include default features that aren't known pgXX version features
                    .filter(|flag| !pgrx.is_feature_flag(flag))
                    .cloned(),
            );
        }

        // when the pgrx test harness is driving the build, drop feature flags the target manifest doesn't define
        if test {
            features.features.retain(|flag| {
                if manifest.features.contains_key(flag) || flag == "pgrx/cshim" {
                    true
                } else {
                    use owo_colors::OwoColorize;
                    println!(
                        "{} feature `{}`",
                        "    Ignoring".bold().yellow(),
                        flag.bold().white()
                    );
                    false
                }
            });
        }

        // no matter what, we need the postgres version we determined to be included in the
        // set of features to compile with
        if !features.features.contains(pg_version.label()) {
            features.features.push(pg_version.label().clone());
        }
    }
}

pub(crate) fn pg_config_and_version(
    pgrx: &Pgrx,
    manifest: &Manifest,
    specified_pg_version: Option<String>,
    user_features: Option<&mut Features>,
    verbose: bool,
) -> eyre::Result<(PgConfig, PgVersionSource)> {
    let pg_version = || {
        if let Some(pg_version) = specified_pg_version {
            // the user gave us an explicit Postgres version to use, so we will
            return Some(PgVersionSource::CliArgument(pg_version));
        } else if let Some(features) = user_features.as_ref() {
            // the user did not give us an explicit Postgres version, so see if there's one in the set
            // of `--feature` flags they gave us
            if let Some(flag) = features.features.iter().find(|flag| pgrx.is_feature_flag(flag)) {
                // use the first feature flag that is a Postgres version we support
                return Some(PgVersionSource::FeatureFlag(flag.clone()));
            }

            // user didn't give us a feature flag that is a Postgres version

            // if they didn't ask for `--no-default-features` lets see if we have a default
            // postgres version feature specified in the manifest
            if !features.no_default_features
                && let Some(default_features) = manifest.features.get("default")
                && let Some(flag) = default_features.iter().find(|flag| pgrx.is_feature_flag(flag))
            {
                return Some(PgVersionSource::DefaultFeature(flag.clone()));
            }
        } else if let Some(default_features) = manifest.features.get("default")
            && let Some(flag) = default_features.iter().find(|flag| pgrx.is_feature_flag(flag))
        {
            return Some(PgVersionSource::DefaultFeature(flag.clone()));
        }

        // we cannot determine the Postgres version the user wants to use
        None
    };

    match pg_version() {
        Some(pg_version) => {
            // we have determined a Postgres version

            modify_features_for_version(pgrx, user_features, manifest, &pg_version, false);
            let pg_config = pgrx.get(pg_version.label())?;

            if verbose {
                display_version_info(&pg_config, &pg_version);
            }

            Ok((pg_config, pg_version))
        }
        None => Err(eyre!("Could not determine which Postgres version feature flag to use")),
    }
}

pub(crate) fn display_version_info(pg_config: &PgConfig, pg_version: &PgVersionSource) {
    use owo_colors::OwoColorize;
    eprintln!(
        "{} {:?} and `pg_config` from {}",
        "       Using".bold().green(),
        pg_version.bold().white(),
        pg_config.path().unwrap().display().cyan()
    );
}

pub(crate) fn get_package_manifest(
    features: &Features,
    package_name: Option<&str>,
    manifest_path: Option<&Path>,
) -> eyre::Result<(Manifest, PathBuf)> {
    let metadata = crate::metadata::metadata(features, manifest_path)
        .wrap_err("couldn't get cargo metadata")?;
    crate::metadata::validate(manifest_path, &metadata)?;
    let package_manifest_path = crate::manifest::manifest_path(&metadata, package_name)
        .wrap_err("Couldn't get manifest path")?;

    Ok((
        Manifest::from_path(&package_manifest_path).wrap_err("Couldn't parse manifest")?,
        package_manifest_path,
    ))
}

pub(crate) fn all_pg_in_both_tomls<'a>(
    manifest: &'a Manifest,
    pgrx: &'a Pgrx,
) -> impl Iterator<Item = eyre::Result<PgConfig>> + 'a {
    // Maybe eventually warn when the Cargo.toml has a version our config.toml doesn't,
    // as it makes sense to further constrain support from the version set pgrx supports,
    // but it doesn't make sense to e.g. not run tests when admin thought it was requested?
    pgrx.iter(PgConfigSelector::All).filter(|result| match result {
        Ok(pg_config) => {
            if let Ok(ver) = pg_config.major_version() {
                // Clumsy: we rely on these features enabling `pgrx/pg{ver}` instead of verifying.
                manifest.features.contains_key(&format!("pg{ver}"))
            } else {
                false // Nonsensical to have no major version for a pg_config?
            }
        }
        // Pass errors along
        _ => true,
    })
}
