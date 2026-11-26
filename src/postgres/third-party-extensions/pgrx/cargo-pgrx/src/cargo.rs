//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::{env, process};

/// Configuration for building a cargo execution
#[derive(Default, Clone, Debug)]
pub struct Cargo {
    subcmd: String,
    features: clap_cargo::Features,
    stdio: [Stdio; 3],
    manifest: Option<PathBuf>,
    package: String,
    log_level: Option<String>,
    target: Option<String>,
    profile: CargoProfile,
    // use a BTreeMap for deterministic order of iteration
    more_args: BTreeMap<String, Vec<String>>,
    // Top-level cargo args placed before the subcommand (e.g. `--config ...`).
    // Kept separate so callers can opt into cdylib-only tweaks without touching
    // subcommand-specific arg ordering.
    top_level_args: Vec<String>,
}

impl Cargo {
    pub fn subcommand(mut self, s: &str) -> Self {
        self.subcmd.clear();
        self.subcmd.push_str(s);
        self
    }

    pub fn std_streams(mut self, streams: [Stdio; 3]) -> Self {
        self.stdio = streams;
        self
    }

    pub fn manifest_path(mut self, path: Option<PathBuf>) -> Self {
        self.manifest = path;
        self
    }

    pub fn package(mut self, package: String) -> Self {
        self.package = package;
        self
    }

    pub fn target(mut self, target: Option<String>) -> Self {
        self.target = target;
        self
    }

    pub fn profile(mut self, profile: CargoProfile) -> Self {
        self.profile = profile;
        self
    }

    pub fn log_level(mut self, level: Option<String>) -> Self {
        self.log_level = level;
        self
    }

    pub fn flag(mut self, flag: impl Into<String>) -> Self {
        self.more_args.insert(flag.into(), Vec::new());
        self
    }

    pub fn features(mut self, features: clap_cargo::Features) -> Self {
        self.features = features;
        self
    }

    /// Apply the `.pgrxsc` retention workaround for cdylib-producing
    /// invocations. See `pgrx_cdylib_config_args` for why this is only safe
    /// to use when the subcommand only links a cdylib (never for `test`,
    /// which also links a test binary that needs normal `--gc-sections` to
    /// prune undefined Postgres `extern "C"` references).
    pub fn with_pgrx_cdylib_workaround(mut self) -> Self {
        self.top_level_args.extend(pgrx_cdylib_config_args());
        self
    }

    #[track_caller]
    pub fn into_command(self) -> process::Command {
        let mut cmd = cargo();

        // top-level cargo args (e.g. `--config ...`) go before the subcommand.
        for arg in &self.top_level_args {
            cmd.arg(arg);
        }

        // subcommand *must* go first among subcommand-relative args
        if !self.subcmd.is_empty() {
            cmd.arg(&self.subcmd);
        } else {
            panic!("`Cargo::into_command` requires a subcommand to be set, was: {self:?}")
        }

        let Self {
            features,
            stdio,
            manifest,
            log_level,
            target,
            profile,
            package,
            subcmd: _,
            more_args,
            top_level_args: _,
        } = self;

        // set most-interesting flags first, like profile, target, and manifest-path
        // so that when we read dumped command lines we can see that info first
        cmd.args(profile.cargo_args());

        if let Some(target) = target {
            cmd.arg("--target").arg(target);
        }
        if let Some(manifest) = manifest {
            cmd.arg("--manifest-path").arg(manifest);
        }
        if !package.is_empty() {
            cmd.arg("--package").arg(package);
        }

        // set std streams
        let [stdin, stdout, stderr] = stdio;
        if let Some(stdio) = stdin.into_stdio() {
            cmd.stdin(stdio);
        }
        if let Some(stdio) = stdout.into_stdio() {
            cmd.stdout(stdio);
        }
        if let Some(stdio) = stderr.into_stdio() {
            cmd.stderr(stdio);
        }

        // set features
        if features.no_default_features {
            cmd.arg("--no-default-features");
        }

        if features.all_features {
            cmd.arg("--all-features");
        }

        if !features.features.is_empty() {
            cmd.arg("--features");
            cmd.arg(features.features.join(" "));
        }

        // And now the miscellaneous build flags!
        let flags = env::var("PGRX_BUILD_FLAGS").unwrap_or_default();
        for arg in flags.split_ascii_whitespace() {
            cmd.arg(arg);
        }

        // set envs
        if let Some(log_level) = log_level {
            cmd.env("RUST_LOG", log_level);
        }

        for (flag, args) in more_args {
            cmd.arg(flag).args(args);
        }

        cmd
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum Stdio {
    Inherit,
    Null,
    #[default]
    Default,
}

impl Stdio {
    fn into_stdio(self) -> Option<process::Stdio> {
        match self {
            Self::Inherit => Some(process::Stdio::inherit()),
            Self::Null => Some(process::Stdio::null()),
            Self::Default => None,
        }
    }
}

pub(crate) fn cargo() -> std::process::Command {
    let cargo = std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into());
    std::process::Command::new(cargo)
}

/// Cargo top-level `--config` arguments that force the linker to retain the
/// `.pgrxsc` section when building the extension cdylib.
///
/// The schema metadata that drives `cargo pgrx schema` lives in a `.pgrxsc`
/// ELF section composed of `#[used]` statics scattered across every CGU. Rust
/// lowers `#[used]` to `@llvm.used`, which on ELF is supposed to mark the
/// containing section with `SHF_GNU_RETAIN` so the linker's default
/// `--gc-sections` pass leaves it alone. That contract is fragile in the wild:
/// it depends on LLVM emitting the flag, binutils honoring it, and LTO not
/// masking the difference. When it breaks, every `.pgrxsc` contribution is
/// dropped and `cargo pgrx schema` fails with "no embedded pgrx schema section
/// found" — which is exactly what we've seen on non-LTO aarch64 Linux builds.
///
/// Passing `-Wl,--no-gc-sections` as a link-arg on non-macOS Unix sidesteps the
/// whole retention contract. The cost is a few KB of unreferenced code surviving
/// in the final cdylib, which is a trade we'll happily make.
///
/// Critically, these args are only injected for cdylib-producing invocations
/// (`cargo build --lib` inside cargo-pgrx install/package/schema paths). They
/// MUST NOT be injected for `cargo test` invocations: rustc uses `--gc-sections`
/// on test binaries to prune the many unreachable `extern "C"` references to
/// Postgres symbols that pgrx-pg-sys declares. Keeping all those call sites
/// retained makes the linker refuse to produce the test executable with a
/// wall of "undefined symbol: CurrentMemoryContext" errors. The test-harness
/// flow (`cargo pgrx test`) still picks up the workaround because the harness
/// re-invokes `cargo-pgrx install` internally, and that path goes through
/// `build_extension` which does apply the workaround.
///
/// This is scoped via a `cfg(...)` target predicate so it only fires when
/// building for a GNU-ld-ish target; macOS (Mach-O, `ld64`, uses `-dead_strip`
/// and doesn't understand this flag) and Windows (MSVC `link.exe`, doesn't
/// speak `-Wl,...`) are excluded.
///
/// Users who set `RUSTFLAGS` or `CARGO_ENCODED_RUSTFLAGS` override this per
/// cargo's rustflags precedence rules. Setting
/// `CARGO_PGRX_DISABLE_GC_SECTIONS_WORKAROUND=1` in the environment also
/// suppresses the injection entirely.
pub(crate) fn pgrx_cdylib_config_args() -> Vec<String> {
    if std::env::var_os("CARGO_PGRX_DISABLE_GC_SECTIONS_WORKAROUND").is_some() {
        return Vec::new();
    }
    vec![
        "--config".to_string(),
        r#"target.'cfg(all(target_family = "unix", not(target_os = "macos")))'.rustflags = ["-C", "link-arg=-Wl,--no-gc-sections"]"#
            .to_string(),
    ]
}

/// Set some environment variables for use downstream (in `pgrx-test` for
/// example). Does nothing if already set.
pub(crate) fn initialize() {
    match (std::env::var_os("CARGO_PGRX"), std::env::current_exe()) {
        (None, Ok(path)) => {
            unsafe {
                std::env::set_var("CARGO_PGRX", path);
            }
            // TODO: Should we set `CARGO_PGRX_{CARGO,RUSTC}` to `RUSTC`/`CARGO`
            // if unset, then prefer those? The issue with `RUSTC`/`CARGO` vars
            // is that they are unset if something invokes e.g. `cargo`
            // directly... This is probably eventually something we'll need, but
            // let's wait until that happens.
        }
        (Some(_), Ok(_)) => {
            // For now I guess we should just hope they're the same.
            // Canonicalizing here's tricky and not guaranteed to behave
            // right... although we could consider calling back into ourselves
            // so something that blindly invokes `cargo-pgrx` instead of
            // `CARGO_PGRX` will do the right thing.
            //
            // In either case if we ever get to the macos-linker-shim work this
            // will have to be slightly firmed up (if `cargo-pgrx` is still going
            // to act as the linker shim.)
        }
        //  bad but not much we can do.
        (_, Err(_)) => {}
    }
}

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub enum CargoProfile {
    /// The default non-release profile, `[profile.dev]`
    #[default]
    Dev,
    /// The default release profile, `[profile.release]`
    Release,
    /// Some other profile, specified by name.
    Profile(String),
}

impl CargoProfile {
    pub fn from_flags(profile: Option<&str>, default: Self) -> eyre::Result<Self> {
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

#[cfg(test)]
mod tests {
    use super::pgrx_cdylib_config_args;

    #[test]
    fn cdylib_config_args_carry_no_gc_sections_workaround() {
        let args = pgrx_cdylib_config_args();
        assert_eq!(args.len(), 2, "expected `--config <value>` pair, got {args:?}");
        assert_eq!(args[0], "--config");
        assert!(
            args[1].contains("--no-gc-sections"),
            "cdylib --config should disable section GC on non-macOS Unix: {}",
            args[1]
        );
        assert!(
            args[1].contains("not(target_os = \"macos\")"),
            "cdylib --config must exclude macOS: {}",
            args[1]
        );
    }
}
