//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::path::Path;

use cargo_toml::Manifest;
use eyre::eyre;

/// Extension to `cargo_toml::Manifest`.
/// Import by adding `use pgrx_pg_config::cargo::PgrxManifestExt;`
/// and extended functions will be available on `Manifest` values.
pub trait PgrxManifestExt {
    /// Package name
    fn package_name(&self) -> eyre::Result<String>;

    /// Package version
    fn package_version(&self) -> eyre::Result<String>;

    /// Resolved string for target library name, either its lib.name,
    /// or package name with hyphens replaced with underscore.
    /// <https://doc.rust-lang.org/cargo/reference/cargo-targets.html#the-name-field>
    fn lib_name(&self) -> eyre::Result<String>;

    /// Resolved string for target artifact name, used for matching on
    /// `cargo_metadata::message::Artifact`.
    fn target_name(&self) -> eyre::Result<String>;

    /// Resolved string for target library name extension filename
    fn lib_filename(&self) -> eyre::Result<String>;
}

impl PgrxManifestExt for Manifest {
    fn package_name(&self) -> eyre::Result<String> {
        match &self.package {
            Some(package) => Ok(package.name.to_owned()),
            None => Err(eyre!("Could not get [package] from manifest.")),
        }
    }

    fn package_version(&self) -> eyre::Result<String> {
        match &self.package {
            Some(package) => match &package.version {
                cargo_toml::Inheritable::Set(version) => Ok(version.to_owned()),
                // This should be impossible to hit, since we use
                // `Manifest::from_path`, which calls `complete_from_path`,
                // which is documented as resolving these. That said, I
                // haven't tested it, and it's not clear how much it
                // actually matters either way, so we just emit an error
                // rather than doing something like `unreachable!()`.
                cargo_toml::Inheritable::Inherited => {
                    Err(eyre!("Workspace-inherited package version are not currently supported."))
                }
            },
            None => Err(eyre!("Could not get [package] from manifest.")),
        }
    }

    fn lib_name(&self) -> eyre::Result<String> {
        match &self.package {
            Some(_) => match &self.lib {
                Some(lib) => match &lib.name {
                    // `cargo_manifest` auto fills lib.name with package.name;
                    // hyphen replaced with underscore if crate type is lib.
                    // So we will always have a lib.name for lib crates.
                    Some(lib_name) => Ok(lib_name.to_owned()),
                    None => Err(eyre!("Could not get [lib] name from manifest.")),
                },
                None => Err(eyre!("Could not get [lib] name from manifest.")),
            },
            None => Err(eyre!("Could not get [lib] name from manifest.")),
        }
    }

    fn target_name(&self) -> eyre::Result<String> {
        let package = self.package_name()?;
        let lib = self.lib_name()?;
        if package.replace('-', "_") == lib {
            Ok(package)
        } else {
            Ok(lib)
        }
    }

    fn lib_filename(&self) -> eyre::Result<String> {
        use std::env::consts::{DLL_PREFIX, DLL_SUFFIX};
        let lib_name = &self.lib_name()?;
        Ok(format!("{DLL_PREFIX}{}{DLL_SUFFIX}", lib_name.replace('-', "_")))
    }
}

/// Helper functions to read `Cargo.toml` and remap error to `eyre::Result`.
pub fn read_manifest<T: AsRef<Path>>(path: T) -> eyre::Result<Manifest> {
    Manifest::from_path(path).map_err(|err| eyre!("Couldn't parse manifest: {err}"))
}
