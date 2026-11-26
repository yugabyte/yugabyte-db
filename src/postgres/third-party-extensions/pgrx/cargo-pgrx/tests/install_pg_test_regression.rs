//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

use pgrx_pg_config::{PgConfigSelector, Pgrx};
use std::path::{Path, PathBuf};
use std::process::Command;

fn cargo_pgrx_bin() -> &'static str {
    env!("CARGO_BIN_EXE_cargo-pgrx")
}

fn workspace_root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR")).parent().expect("cargo-pgrx lives under the workspace")
}

fn unit_tests_manifest_path() -> PathBuf {
    workspace_root().join("pgrx-unit-tests").join("Cargo.toml")
}

fn preferred_pg_config() -> Option<(String, PathBuf)> {
    // This is an integration-style regression test, so we need a real configured
    // Postgres installation rather than a mocked path. If the local environment
    // has not been initialized yet, skip instead of failing noisily for every
    // developer who runs the test suite before `cargo pgrx init`.
    let pgrx = match Pgrx::from_config() {
        Ok(pgrx) => pgrx,
        Err(err) => {
            eprintln!("skipping install_pg_test_regression: could not load pgrx config: {err}");
            return None;
        }
    };

    // The test only needs one working `pg_config`, but using an installed entry
    // from the pgrx-managed config keeps the execution path identical to the
    // normal workflow. We collect all configured versions up front so we can
    // prefer one deterministically rather than relying on iteration order.
    let mut configs = pgrx
        .iter(PgConfigSelector::All)
        .filter_map(Result::ok)
        .filter_map(|pg_config| Some((pg_config.major_version().ok()?, pg_config.path()?)))
        .collect::<Vec<_>>();

    if configs.is_empty() {
        eprintln!("skipping install_pg_test_regression: no configured pg_config entries");
        return None;
    }

    // Prefer pg18 when it exists because that is the newest version currently
    // covered by the in-tree test extension. If it is not installed locally,
    // fall back to the highest configured version so the regression still runs
    // on developer machines with a partial setup.
    configs.sort_by_key(|(major, _)| *major);
    let preferred = configs
        .iter()
        .position(|(major, _)| *major == 18)
        .map(|index| configs.swap_remove(index))
        .unwrap_or_else(|| configs.pop().expect("non-empty after is_empty check"));

    Some((format!("pg{}", preferred.0), preferred.1))
}

#[test]
fn install_test_extension_handles_mid_stream_schema_sentinel() {
    // This regression belongs at the `cargo-pgrx` layer, not in a plain unit
    // test for section decoding, because the original failure happened during
    // `cargo pgrx install --test`. The bad behavior only surfaced once a real
    // extension artifact had been built, linked, reopened, and decoded through
    // the same command path users exercise in practice.
    //
    // `pgrx-unit-tests` is a good fixture here because it has a large number of
    // `#[pg_test]` functions spread across many modules. That creates a large
    // embedded schema section whose sentinel entry is not isolated at the start
    // or end of the section. This test makes sure that full command still works
    // when the sentinel appears in the middle of many ordinary SQL entities.
    let Some((pg_feature, pg_config_path)) = preferred_pg_config() else {
        return;
    };

    // Drive the already-built `cargo-pgrx` test binary directly. Using
    // `CARGO_BIN_EXE_cargo-pgrx` avoids recursively invoking a second Cargo
    // build of this crate just to start the command under test.
    let output = Command::new(cargo_pgrx_bin())
        .current_dir(workspace_root())
        .arg("pgrx")
        .arg("install")
        .arg("--test")
        .arg("--pg-config")
        .arg(&pg_config_path)
        .arg("--features")
        .arg(format!(
            "{pg_feature} pg_test{}",
            if cfg!(all(
                any(target_os = "linux", target_os = "macos"),
                any(target_arch = "x86_64", target_arch = "aarch64")
            )) {
                ""
            } else {
                " cshim"
            }
        ))
        .arg("--no-default-features")
        .arg("--manifest-path")
        .arg(unit_tests_manifest_path())
        .output()
        .expect("cargo-pgrx install --test should launch");

    // The command emits different classes of progress messages to different
    // streams: human-facing command progress tends to land on stdout, while the
    // schema discovery summary is emitted on stderr. Keep both around so a
    // regression prints the full picture instead of hiding whichever side the
    // interesting failure happened on.
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert!(
        output.status.success(),
        "cargo-pgrx install --test failed\nstdout:\n{stdout}\nstderr:\n{stderr}"
    );

    // We assert on two stable milestones:
    // 1. the extension install completed far enough to report installation, and
    // 2. schema generation actually decoded entities from the linked binary.
    //
    // That second check matters because the historical bug did not break the
    // build step; it broke the post-build schema scan. Looking only at the exit
    // status would make it harder to distinguish "command ran" from "command
    // exercised the decoder path we care about."
    assert!(
        stdout.contains("installing pgrx_unit_tests") && stderr.contains("SQL entities:"),
        "cargo-pgrx install --test succeeded but did not appear to run the expected install and schema-generation steps\nstdout:\n{stdout}\nstderr:\n{stderr}"
    );
}
