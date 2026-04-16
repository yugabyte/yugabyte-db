//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/// Represents a selected cargo profile
///
/// Generally chosen from flags like `--release`, `--profile <profile name>`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CargoProfile {
    /// The default non-release profile, `[profile.dev]`
    Dev,
    /// The default release profile, `[profile.release]`
    Release,
    /// Some other profile, specified by name.
    Profile(String),
}

impl CargoProfile {
    pub fn from_flags(profile: Option<&str>, default: CargoProfile) -> eyre::Result<Self> {
        match profile {
            // Cargo treats `--profile release` the same as `--release`.
            Some("release") => Ok(Self::Release),
            // Cargo has two names for the debug profile, due to legacy
            // reasons...
            Some("debug") | Some("dev") => Ok(Self::Dev),
            Some(profile) => Ok(Self::Profile(profile.into())),
            None => Ok(default),
        }
    }

    pub fn cargo_args(&self) -> Vec<String> {
        match self {
            Self::Dev => vec![],
            Self::Release => vec!["--release".into()],
            Self::Profile(p) => vec!["--profile".into(), p.into()],
        }
    }

    pub fn name(&self) -> &str {
        match self {
            Self::Dev => "dev",
            Self::Release => "release",
            Self::Profile(p) => p,
        }
    }

    pub fn target_subdir(&self) -> &str {
        match self {
            Self::Dev => "debug",
            Self::Release => "release",
            Self::Profile(p) => p,
        }
    }
}
