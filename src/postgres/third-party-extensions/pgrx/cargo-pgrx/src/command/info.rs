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
use pgrx_pg_config::Pgrx;
use std::borrow::Cow;

/// Provides information about pgrx-managed development environment
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Info {
    #[clap(subcommand)]
    command: Subcommand,
}

#[derive(clap::Subcommand, Debug)]
pub(crate) enum Subcommand {
    /// Print path to a base version of Postgres build
    ///
    /// cargo pgrx info path 15 #=> ~/.pgrx/15.2/pgrx-install
    Path {
        /// Postgres version (12, 13, 14, 15...)
        pg_ver: String,
    },
    /// Print path to pg_config for a base version of Postgres
    ///
    /// cargo pgrx info pg-config 15 #=> ~/.pgrx/15.2/pgrx-install/bin/pg_config
    PgConfig {
        /// Postgres version (12, 13, 14, 15...)
        pg_ver: String,
    },
    /// Print specific version for a base Postgres version
    ///
    /// cargo pgrx info version 15 #=> 15.2
    Version {
        /// Postgres version (12, 13, 14, 15...)
        pg_ver: String,
    },
}

impl CommandExecute for Info {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(self) -> eyre::Result<()> {
        let config = Pgrx::from_config()?;
        match self.command {
            Subcommand::Path { ref pg_ver } => {
                println!(
                    "{}",
                    config
                        .get(&version(pg_ver))?
                        .parent_path()
                        .parent()
                        .ok_or(eyre::Error::msg("can't get path"))?
                        .display()
                );
            }
            Subcommand::PgConfig { ref pg_ver } => {
                println!(
                    "{}",
                    config
                        .get(&version(pg_ver))?
                        .path()
                        .ok_or(eyre::Error::msg("can't get path"))?
                        .display()
                );
            }
            Subcommand::Version { ref pg_ver } => {
                println!("{}", config.get(&version(pg_ver))?.version()?);
            }
        }
        Ok(())
    }
}

fn version(ver: &str) -> Cow<str> {
    if ver.starts_with("pg") {
        Cow::Borrowed(ver)
    } else {
        Cow::Owned(format!("pg{ver}"))
    }
}
