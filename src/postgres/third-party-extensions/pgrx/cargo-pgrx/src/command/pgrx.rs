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
use owo_colors::OwoColorize;
use std::path::Path;

#[derive(clap::Args, Debug)]
#[clap(about, author)]
pub(crate) struct Pgrx {
    #[clap(subcommand)]
    subcommand: CargoPgrxSubCommands,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
}

impl CommandExecute for Pgrx {
    fn execute(self) -> eyre::Result<()> {
        self.subcommand.execute()
    }
}

#[derive(clap::Subcommand, Debug)]
enum CargoPgrxSubCommands {
    Init(super::init::Init),
    Info(super::info::Info),
    Start(super::start::Start),
    Stop(super::stop::Stop),
    Status(super::status::Status),
    New(super::new::New),
    Install(super::install::Install),
    Package(super::package::Package),
    Schema(super::schema::Schema),
    Run(super::run::Run),
    Connect(super::connect::Connect),
    Test(super::test::Test),
    Get(super::get::Get),
    Cross(super::cross::Cross),
    Upgrade(super::upgrade::Upgrade),
}

impl CommandExecute for CargoPgrxSubCommands {
    fn execute(self) -> eyre::Result<()> {
        use CargoPgrxSubCommands::*;
        check_for_sql_generator_binary()?;
        match self {
            Init(c) => c.execute(),
            Info(c) => c.execute(),
            Start(c) => c.execute(),
            Stop(c) => c.execute(),
            Status(c) => c.execute(),
            New(c) => c.execute(),
            Install(c) => c.execute(),
            Package(c) => c.execute(),
            Schema(c) => c.execute(),
            Run(c) => c.execute(),
            Connect(c) => c.execute(),
            Test(c) => c.execute(),
            Get(c) => c.execute(),
            Cross(c) => c.execute(),
            Upgrade(c) => c.execute(),
        }
    }
}

/// A temporary check to help users from 0.2 or 0.3 know to take manual migration steps.
fn check_for_sql_generator_binary() -> eyre::Result<()> {
    if Path::new("src/bin/sql-generator.rs").exists() {
        // We explicitly do not want to return a spantraced error here.
        println!("{}", "\
            Found `pgrx` 0.2-0.3 series SQL generation while using `cargo-pgrx` 0.4 series.
            
We've updated our SQL generation method, it's much faster! Please follow the upgrading steps listed in https://github.com/pgcentralfoundation/pgrx/releases/tag/v0.4.0.

Already done that? You didn't delete `src/bin/sql-generator.rs` yet, so you're still seeing this message.\
        ".red().bold());
        std::process::exit(1)
    } else {
        Ok(())
    }
}
