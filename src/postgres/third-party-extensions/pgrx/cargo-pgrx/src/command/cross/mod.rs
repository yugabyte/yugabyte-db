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
pub(crate) mod pgrx_target;

/// Commands having to do with cross-compilation. (Experimental)
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Cross {
    #[command(subcommand)]
    pub(crate) subcommand: CargoPgrxCrossSubCommands,
}

impl CommandExecute for Cross {
    fn execute(self) -> eyre::Result<()> {
        self.subcommand.execute()
    }
}

/// Subcommands relevant to cross-compilation.
#[derive(clap::Subcommand, Debug)]
pub(crate) enum CargoPgrxCrossSubCommands {
    PgrxTarget(pgrx_target::PgrxTarget),
}

impl CommandExecute for CargoPgrxCrossSubCommands {
    fn execute(self) -> eyre::Result<()> {
        use CargoPgrxCrossSubCommands::*;
        match self {
            PgrxTarget(target_info) => target_info.execute(),
        }
    }
}
