//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use eyre::eyre;
use pgrx_pg_config::{
    PgConfig, PgConfigSelector, Pgrx, SUPPORTED_VERSIONS, is_supported_major_version,
};

pub mod build;

fn is_for_release() -> bool {
    env_tracked("PGRX_PG_SYS_GENERATE_BINDINGS_FOR_RELEASE").as_deref() == Some("1")
}

/// Determines which `pg_config` is to be used, based on a combination of pgrx' internal knowledge
/// of supported Postgres versions and `cargo` options/feature flags.
pub fn detect_pg_config() -> eyre::Result<Vec<(u16, PgConfig)>> {
    let pg_configs: Vec<(u16, PgConfig)> = if is_for_release() {
        // This does not cross-check config.toml and Cargo.toml versions, as it is release infra.
        Pgrx::from_config()?.iter(PgConfigSelector::All)
            .map(|r| r.expect("invalid pg_config"))
            .map(|c| (c.major_version().expect("invalid major version"), c))
            .filter_map(|t| {
                if is_supported_major_version(t.0) {
                    Some(t)
                } else {
                    println!(
                        "cargo:warning={} contains a configuration for pg{}, which pgrx does not support.",
                        Pgrx::config_toml()
                            .expect("Could not get PGRX configuration TOML")
                            .to_string_lossy(),
                        t.0
                    );
                    None
                }
            })
            .collect()
    } else {
        let mut found = Vec::new();
        for pgver in SUPPORTED_VERSIONS() {
            if env_tracked(&format!("CARGO_FEATURE_PG{}", pgver.major)).is_some() {
                found.push(pgver);
            }
        }
        let found_ver = match &found[..] {
            [ver] => ver,
            [] => {
                return Err(eyre!(
                    "Did not find `pg$VERSION` feature. `pgrx-pg-sys` requires one of {} to be set",
                    SUPPORTED_VERSIONS()
                        .iter()
                        .map(|pgver| format!("`pg{}`", pgver.major))
                        .collect::<Vec<_>>()
                        .join(", ")
                ));
            }
            versions => {
                return Err(eyre!(
                    "Multiple `pg$VERSION` features found.\n`--no-default-features` may be required.\nFound: {}",
                    versions
                        .iter()
                        .map(|version| format!("pg{}", version.major))
                        .collect::<Vec<String>>()
                        .join(", ")
                ));
            }
        };

        let found_major = found_ver.major;
        if let Ok(pg_config) = PgConfig::from_env() {
            let major_version = pg_config.major_version()?;

            if major_version != found_major {
                panic!(
                    "Feature flag `pg{found_major}` does not match version from the environment-described PgConfig (`{major_version}`)"
                )
            }
            vec![(major_version, pg_config)]
        } else {
            let specific = Pgrx::from_config()?.get(&format!("pg{}", found_ver.major))?;
            vec![(found_ver.major, specific)]
        }
    };
    Ok(pg_configs)
}

fn env_tracked(s: &str) -> Option<String> {
    // a **sorted** list of environment variable keys that cargo might set that we don't need to track
    // these were picked out, by hand, from: https://doc.rust-lang.org/cargo/reference/environment-variables.html
    const CARGO_KEYS: &[&str] = &[
        "BROWSER",
        "DEBUG",
        "DOCS_RS",
        "HOST",
        "HTTP_PROXY",
        "HTTP_TIMEOUT",
        "NUM_JOBS",
        "OPT_LEVEL",
        "OUT_DIR",
        "PATH",
        "PROFILE",
        "TARGET",
        "TERM",
    ];

    let is_cargo_key =
        s.starts_with("CARGO") || s.starts_with("RUST") || CARGO_KEYS.binary_search(&s).is_ok();

    if !is_cargo_key {
        // if it's an envar that cargo gives us, we don't want to ask it to rerun build.rs if it changes
        // we'll let cargo figure that out for itself, and doing so, depending on the key, seems to
        // cause cargo to rerun build.rs every time, which is terrible
        println!("cargo:rerun-if-env-changed={s}");
    }
    std::env::var(s).ok()
}
