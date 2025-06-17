//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use cargo_edit::{registry_url, CertsSource, Dependency, IndexCache, LocalManifest};
use eyre::eyre;
use std::path::{Path, PathBuf};
use toml_edit::KeyMut;
use tracing::{debug, error, info, warn};

use crate::CommandExecute;

/// Upgrade pgrx crate versions in `Cargo.toml`.
/// Defaults to latest.
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Upgrade {
    /// Specify a version to upgrade to, rather than defaulting to the latest
    /// version.
    #[clap(long)]
    pub(crate) to: Option<String>, //TODO: semver not string

    /// Path to the manifest file (usually Cargo.toml). Defaults to
    /// "./Cargo.toml" in the working directory.
    #[clap(long = "manifest-path", short)]
    pub(crate) manifest_path: Option<PathBuf>,

    /// Flag for upgrading PGRX to pre-release versions.
    #[clap(long = "include-prereleases")]
    pub(crate) include_prereleases: bool,

    /// Select a package within the current workspace within which to upgrade
    /// pgrx package versions. Defaults to the root manifest in the working
    /// directory.
    #[clap(long, short)]
    pub(crate) package: Option<String>,

    /// Dry-run - if this flag is set, Cargo.toml will not be modified.
    /// Instead, this command will print the text of the new Cargo.toml
    /// that would have been generated if it was modified.
    #[clap(long = "dry-run", short = 'n')]
    pub(crate) dry_run: bool,
}

fn set_dep_source_version<A: AsRef<str>>(src: &mut cargo_edit::Source, new_version: A) {
    match src {
        cargo_edit::Source::Registry(reg) => reg.version = new_version.as_ref().to_string(),
        cargo_edit::Source::Path(path) => path.version = Some(new_version.as_ref().to_string()),
        cargo_edit::Source::Git(git) => git.version = Some(new_version.as_ref().to_string()),
        cargo_edit::Source::Workspace(_ws) => {
            error!(
                "Cannot upgrade the version of \
                a package because it is set in the \
                workspace, please run `cargo pgrx \
                upgrade` in the workspace directory."
            );
        }
    }
}
fn get_dep_source_version(src: &cargo_edit::Source) -> Option<&String> {
    match src {
        cargo_edit::Source::Registry(reg) => Some(&reg.version),
        cargo_edit::Source::Path(path) => path.version.as_ref(),
        cargo_edit::Source::Git(git) => git.version.as_ref(),
        cargo_edit::Source::Workspace(_ws) => {
            error!(
                "Cannot fetch the version of \
                a package because it is set in the \
                workspace, please run `cargo pgrx \
                upgrade` in the workspace directory."
            );
            None
        }
    }
}

#[tracing::instrument(level = "error")]
fn replace_version(
    new_version: &str,
    crate_root: &Path,
    key: &mut toml_edit::KeyMut,
    dep: &mut toml_edit::Item,
    mut parsed_dep: Dependency,
    mut source: cargo_edit::Source,
) -> eyre::Result<()> {
    let dep_name = key.get();
    let ver_maybe = get_dep_source_version(&source);
    match ver_maybe {
        Some(v) => {
            debug!("{dep_name} version is {v}")
        }
        None => return Err(eyre!("No version field for {dep_name}, cannot upgrade.")),
    }
    set_dep_source_version(&mut source, new_version);
    parsed_dep = parsed_dep.set_source(source);

    parsed_dep.update_toml(crate_root, key, dep);
    Ok(())
}
impl Upgrade {
    #[tracing::instrument(level = "error", skip(self))]
    fn update_dep(
        &self,
        path: &PathBuf,
        mut key: KeyMut,
        dep: &mut toml_edit::Item,
    ) -> eyre::Result<()> {
        let mut index = IndexCache::new(CertsSource::Native);
        let dep_name_string = key.get().to_string();
        let dep_name = dep_name_string.as_str();
        let parsed_dep: Dependency = match Dependency::from_toml(path.as_path(), dep_name, dep) {
            Ok(dependency) => dependency,
            Err(e) => {
                return Err(eyre!(
                    "Could not parse dependency \
                entry for {dep_name} due to error: {e}"
                ))
            }
        };
        let reg_url = registry_url(path, parsed_dep.registry())
            .map_err(|e| eyre!("Unable to fetch registry URL for path: {e}"))?;
        let index =
            index.index(&reg_url).map_err(|e| eyre!("Unable to get registry index: {e}"))?;
        let target_version = match self.to {
            Some(ref ver) => Some(ver.clone()),
            None => {
                let krate = index.krate(dep_name).map_err(|e| {
                    eyre!("The crate `{dep_name}` could not be found in registry index due {e}.")
                })?;
                let versions = krate.as_ref().map(|k| k.versions.as_slice()).unwrap_or_default();
                let dependency =
                    cargo_edit::find_latest_version(versions, self.include_prereleases, None)
                        .ok_or_else(|| {
                            eyre!("Unable to fetch the latest version for crate {dep_name}.")
                        })?;

                dependency.version().map(|s| s.to_string())
            }
        };
        let target_version = match target_version {
            Some(ver) => ver,
            None => {
                return Err(eyre!(
                    "Unable to update {dep_name} \
                , no provided crate version and a latest version \
                could not be retrieved from the registry."
                ))
            }
        };

        // As of cargo-edit version 0.12.2, dep.source will always be a Some()
        // if the Dependency struct was instantiated via from_toml().
        let source = match parsed_dep.source().cloned() {
            Some(src) => src,
            None => {
                return Err(eyre!(
                    "Dependency {dep_name}'s source was \
                parsed as None by cargo-edit."
                ))
            }
        };
        debug!(
            "Found dependency {dep_name} with current \
            source {source:#?}"
        );
        match &source {
            cargo_edit::Source::Registry(_) => {
                if get_dep_source_version(&source).is_some() {
                    replace_version(
                        target_version.as_str(),
                        path.as_path(),
                        &mut key,
                        dep,
                        parsed_dep,
                        source,
                    )?;
                } else {
                    info!("No version specified for {dep_name}, not upgrading.");
                }
            }
            cargo_edit::Source::Path(_) => {
                warn!(
                    "Cannot upgrade the version of \
                        {dep_name} because it is a path (local) dependency."
                )
            }
            cargo_edit::Source::Git(_) => {
                warn!(
                    "Cannot upgrade the version of \
                        {dep_name} because it is a git dependency."
                )
            }
            cargo_edit::Source::Workspace(_) => {
                warn!(
                    "Cannot upgrade the version of \
                        {dep_name} because it is set in the \
                        workspace, please run `cargo pgrx \
                        upgrade` in the workspace directory."
                )
            }
        }
        Ok(())
    }

    #[allow(deprecated)] // the API changes to toml_edit are quite complex!
    fn process_manifest(&self, path: &PathBuf, manifest: &mut LocalManifest) -> eyre::Result<()> {
        const RELEVANT_PACKAGES: [&str; 6] = [
            "pgrx",
            "pgrx-macros",
            "pgrx-pg-config",
            "pgrx-pg-sys",
            "pgrx-sql-entity-graph",
            "pgrx-tests",
        ];

        for dep_table in manifest.get_dependency_tables_mut() {
            for dep_name in RELEVANT_PACKAGES {
                let decor = dep_table.key_decor(dep_name).cloned();
                if let Some((key, dep)) = dep_table.get_key_value_mut(dep_name) {
                    self.update_dep(path, key, dep)?;
                    // Workaround since update_toml() doesn't preserve comments
                    if let Some(dec) = dep_table.key_decor_mut(dep_name) {
                        if let Some(prefix) = decor.as_ref().and_then(|val| val.prefix().cloned()) {
                            dec.set_prefix(prefix)
                        }
                        if let Some(suffix) = decor.as_ref().and_then(|val| val.suffix().cloned()) {
                            dec.set_suffix(suffix)
                        }
                    }
                } else {
                    debug!(
                        "Manifest does not contain a dependency entry for \
                        {dep_name}"
                    );
                }
            }
        }
        if !self.dry_run {
            manifest
                .write()
                .map_err(|err| eyre!("Unable to write the updated Cargo.toml to disk: {err}"))?;
        } else {
            let generated_toml = manifest.to_string();
            println!("{generated_toml}");
        }
        Ok(())
    }
}

impl CommandExecute for Upgrade {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        // Canonicalize because cargo-edit does not accept relative paths.
        let path = std::fs::canonicalize(
            self.manifest_path.clone().unwrap_or(PathBuf::from("./Cargo.toml")),
        )?;

        let mut manifest = cargo_edit::LocalManifest::find(Some(&path))
            .map_err(|e| eyre!("Error opening manifest: {e}"))?;

        // Attempt to determine if this is a workspace and, if so, should we
        // navigate to a crate contained within.
        let parse_workspace = cargo_toml::Manifest::from_path(&path)
            .map_err(|e| eyre!("Failed to parse workspace manifest using cargo_toml: {e}"))?;

        if let Some(package) = self.package.clone() {
            // Equivalent to manifest.manifest.data.contains_table("workspace"); but more robust
            let workspace = &parse_workspace.workspace;

            match (parse_workspace.package.map(|pak| pak.name() == package.as_str()), workspace) {
                // Name of the package matches the name of the root workspace
                // crate, do not dig for a contained package.
                (Some(true), _) => self.process_manifest(&path, &mut manifest),
                // Contained package specified and this is a workspace,
                // OR workspace has no root package name.
                // find the correct manifest for the child crate.
                (Some(false), Some(work)) | (None, Some(work)) => {
                    // Unfortunately cargo_toml::Workspace provides the member
                    // list as directories but not as package names, so we have
                    // to do a little bit of finagling here.
                    let mut child_path_maybe = None;
                    for member in &work.members {
                        if member == &package {
                            // Canonicalized, should have a parent path
                            let root_path = path.parent().ok_or(eyre!(
                                "Failed to canonicalize path, no \
                                parent directory found."
                            ))?;
                            let member_path = root_path.join(member).join("Cargo.toml");
                            debug!("Generated child path {:#?}", &member_path);
                            child_path_maybe = Some(member_path);
                        }
                    }
                    let child_path = child_path_maybe
                        .ok_or(eyre!("Package {package} not found in workspace {path:#?}"))?;

                    let mut child_manifest = LocalManifest::find(Some(&child_path))
                        .map_err(|e| eyre!("Error opening manifest: {e}"))?;

                    self.process_manifest(&child_path, &mut child_manifest)
                }
                // No name and this is not a workspace, why is a package name
                // specified?
                (None, None) => Err(eyre!(
                    "Package argument provided but the
                        manifest at the path {path:#?} has no name and 
                        is not a workspace, please check Cargo.toml's 
                        validity."
                )),
                // Non-workspace crate, and the name does not match.
                (Some(false), None) => Err(eyre!(
                    "Package argument provided but the
                        manifest at the path {path:#?} appears to be a regular
                        single-crate workspace, with no child crates."
                )),
            }
        } else {
            // No specified package, go right to the manifest at the
            // specified directory.
            self.process_manifest(&path, &mut manifest)?;
            Ok(())
        }
    }
}
