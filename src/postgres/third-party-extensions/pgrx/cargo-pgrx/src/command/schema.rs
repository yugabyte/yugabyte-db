//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::command::get::{find_control_file, get_property};
use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::profile::CargoProfile;
use crate::CommandExecute;
use cargo_toml::Manifest;
use eyre::WrapErr;
use object::read::macho::MachOFatFile32;
use owo_colors::OwoColorize;
use pgrx_pg_config::cargo::PgrxManifestExt;
use pgrx_pg_config::{get_target_dir, PgConfig, Pgrx};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Stdio;

/// Generate extension schema files
#[derive(clap::Args, Debug)]
#[clap(author)]
pub(crate) struct Schema {
    /// Package to build (see `cargo help pkgid`)
    #[clap(long, short)]
    package: Option<String>,
    /// Path to Cargo.toml
    #[clap(long, value_parser)]
    manifest_path: Option<PathBuf>,
    /// Build in test mode (for `cargo pgrx test`)
    #[clap(long)]
    test: bool,
    /// Do you want to run against pg13, pg14, pg15, pg16, or pg17?
    pg_version: Option<String>,
    /// Compile for release mode (default is debug)
    #[clap(long, short)]
    release: bool,
    /// Specific profile to use (conflicts with `--release`)
    #[clap(long)]
    profile: Option<String>,
    /// The `pg_config` path (default is first in $PATH)
    #[clap(long, short = 'c', value_parser)]
    pg_config: Option<PathBuf>,
    #[clap(flatten)]
    features: clap_cargo::Features,
    /// A path to output a produced SQL file (default is `stdout`)
    #[clap(long, short, value_parser)]
    out: Option<PathBuf>,
    /// A path to output a produced GraphViz DOT file
    #[clap(long, short, value_parser)]
    dot: Option<PathBuf>,
    #[clap(long)]
    target: Option<String>,
    #[clap(from_global, action = ArgAction::Count)]
    verbose: u8,
    /// Skip building a fresh extension shared object.
    #[clap(long)]
    skip_build: bool,
}

impl CommandExecute for Schema {
    #[tracing::instrument(level = "error", skip(self))]
    fn execute(mut self) -> eyre::Result<()> {
        let log_level = if let Ok(log_level) = std::env::var("RUST_LOG") {
            Some(log_level)
        } else {
            match self.verbose {
                0 => Some("warn".into()),
                1 => Some("info".into()),
                2 => Some("debug".into()),
                _ => Some("trace".into()),
            }
        };

        let pgrx = Pgrx::from_config()?;
        let (package_manifest, package_manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_ref(),
            self.manifest_path.as_ref(),
        )?;
        let (pg_config, _pg_version) = pg_config_and_version(
            &pgrx,
            &package_manifest,
            self.pg_version.clone(),
            Some(&mut self.features),
            true,
        )?;

        let profile = CargoProfile::from_flags(
            self.profile.as_deref(),
            if self.release { CargoProfile::Release } else { CargoProfile::Dev },
        )?;

        generate_schema(
            &pg_config,
            self.manifest_path.as_ref(),
            self.package.as_ref(),
            package_manifest_path,
            &profile,
            self.test,
            &self.features,
            self.target.as_ref().map(|x| x.as_str()),
            self.out.as_ref(),
            self.dot,
            log_level,
            self.skip_build,
            &mut vec![],
        )
    }
}

#[tracing::instrument(level = "error", skip_all, fields(
    pg_version = %pg_config.version()?,
    profile = ?profile,
    test = is_test,
    path = path.as_ref().map(|path| tracing::field::display(path.as_ref().display())),
    dot,
    features = ?features.features,
))]
pub(crate) fn generate_schema(
    pg_config: &PgConfig,
    user_manifest_path: Option<impl AsRef<Path>>,
    user_package: Option<&String>,
    package_manifest_path: impl AsRef<Path>,
    profile: &CargoProfile,
    is_test: bool,
    features: &clap_cargo::Features,
    target: Option<&str>,
    path: Option<impl AsRef<std::path::Path>>,
    dot: Option<impl AsRef<std::path::Path>>,
    log_level: Option<String>,
    skip_build: bool,
    output_tracking: &mut Vec<PathBuf>,
) -> eyre::Result<()> {
    let manifest = Manifest::from_path(&package_manifest_path)?;
    let (control_file, _extname) = find_control_file(&package_manifest_path)?;

    let flags = std::env::var("PGRX_BUILD_FLAGS").unwrap_or_default();

    let features_arg = features.features.join(" ");

    let package_name = if let Some(user_package) = user_package {
        user_package.clone()
    } else {
        manifest.package_name()?
    };
    let lib_name = manifest.lib_name()?;
    let lib_filename = manifest.lib_filename()?;

    if !skip_build {
        // NB:  The only path where this happens is via the command line using `cargo pgrx schema`
        first_build(
            user_manifest_path.as_ref(),
            profile,
            features,
            log_level.clone(),
            is_test,
            &features_arg,
            &flags,
            target,
            &package_name,
        )?;
    };

    let symbols = compute_symbols(profile, &lib_filename, target)?;

    let mut out_path = None;
    if let Some(path) = path.as_ref() {
        let x = path.as_ref().to_str().expect("`path` is not a valid UTF8 string.");
        out_path = Some(x.to_string());
    }

    let mut out_dot = None;
    if let Some(dot) = dot.as_ref() {
        let x = dot.as_ref().to_str().expect("`dot` is not a valid UTF8 string.");
        out_dot = Some(x.to_string());
    };

    let codegen = compute_codegen(
        control_file,
        package_manifest_path,
        &symbols,
        &lib_name,
        out_path,
        out_dot,
    )?;

    let embed = {
        let mut embed = tempfile::NamedTempFile::new()?;
        embed.write_all(codegen.as_bytes())?;
        embed.flush()?;
        embed
    };

    if let Some(out_path) = path.as_ref() {
        if let Some(parent) = out_path.as_ref().parent() {
            std::fs::create_dir_all(parent).wrap_err("Could not create parent directory")?;
        }
        output_tracking.push(out_path.as_ref().to_path_buf());
    }

    if let Some(dot_path) = dot.as_ref() {
        tracing::info!(dot = %dot_path.as_ref().display(), "Writing Graphviz DOT");
    }

    second_build(
        user_manifest_path.as_ref(),
        features,
        log_level.clone(),
        &features_arg,
        &flags,
        embed.path(),
        &package_name,
    )?;

    compute_sql(&package_name, &manifest)?;

    Ok(())
}

fn compute_symbols(
    profile: &CargoProfile,
    lib_filename: &str,
    target: Option<&str>,
) -> eyre::Result<Vec<String>> {
    use object::Object;
    use std::collections::HashSet;

    // Inspect the symbol table for a list of `__pgrx_internals` we should have the generator call
    let mut lib_so = get_target_dir()?;
    if let Some(target) = target {
        lib_so.push(target);
    }
    lib_so.push(profile.target_subdir());
    lib_so.push(lib_filename);

    let lib_so_data = std::fs::read(&lib_so).wrap_err("couldn't read extension shared object")?;
    let lib_so_obj_file =
        parse_object(&lib_so_data).wrap_err("couldn't parse extension shared object")?;
    let lib_so_exports =
        lib_so_obj_file.exports().wrap_err("couldn't get exports from extension shared object")?;

    // Some users reported experiencing duplicate entries if we don't ensure `fns_to_call`
    // has unique entries.
    let mut fns_to_call = HashSet::new();
    for export in lib_so_exports {
        let name = std::str::from_utf8(export.name())?.to_string();
        #[cfg(target_os = "macos")]
        let name = {
            // Mac will prefix symbols with `_` automatically, so we remove it to avoid getting
            // two.
            let mut name = name;
            let rename = name.split_off(1);
            assert_eq!(name, "_");
            rename
        };

        if name.starts_with("__pgrx_internals") {
            fns_to_call.insert(name);
        }
    }
    let mut seen_schemas = Vec::new();
    let mut num_funcs = 0_usize;
    let mut num_triggers = 0_usize;
    let mut num_types = 0_usize;
    let mut num_enums = 0_usize;
    let mut num_sqls = 0_usize;
    let mut num_ords = 0_usize;
    let mut num_hashes = 0_usize;
    let mut num_aggregates = 0_usize;
    for func in &fns_to_call {
        if func.starts_with("__pgrx_internals_schema_") {
            let schema =
                func.split('_').nth(5).expect("Schema extern name was not of expected format");
            seen_schemas.push(schema);
        } else if func.starts_with("__pgrx_internals_fn_") {
            num_funcs += 1;
        } else if func.starts_with("__pgrx_internals_trigger_") {
            num_triggers += 1;
        } else if func.starts_with("__pgrx_internals_type_") {
            num_types += 1;
        } else if func.starts_with("__pgrx_internals_enum_") {
            num_enums += 1;
        } else if func.starts_with("__pgrx_internals_sql_") {
            num_sqls += 1;
        } else if func.starts_with("__pgrx_internals_ord_") {
            num_ords += 1;
        } else if func.starts_with("__pgrx_internals_hash_") {
            num_hashes += 1;
        } else if func.starts_with("__pgrx_internals_aggregate_") {
            num_aggregates += 1;
        }
    }

    eprintln!(
        "{} {} SQL entities: {} schemas ({} unique), {} functions, {} types, {} enums, {} sqls, {} ords, {} hashes, {} aggregates, {} triggers",
        "  Discovered".bold().green(),
        fns_to_call.len().to_string().bold().cyan(),
        seen_schemas.len().to_string().bold().cyan(),
        seen_schemas.iter().collect::<std::collections::HashSet<_>>().len().to_string().bold().cyan(),
        num_funcs.to_string().bold().cyan(),
        num_types.to_string().bold().cyan(),
        num_enums.to_string().bold().cyan(),
        num_sqls.to_string().bold().cyan(),
        num_ords.to_string().bold().cyan(),
        num_hashes.to_string().bold().cyan(),
        num_aggregates.to_string().bold().cyan(),
        num_triggers.to_string().bold().cyan(),
    );

    tracing::debug!("Collecting {} SQL entities", fns_to_call.len());

    Ok(fns_to_call.into_iter().collect())
}

fn first_build(
    user_manifest_path: Option<&impl AsRef<Path>>,
    profile: &CargoProfile,
    features: &clap_cargo::Features,
    log_level: Option<String>,
    is_test: bool,
    features_arg: &str,
    flags: &str,
    target: Option<&str>,
    package_name: &str,
) -> eyre::Result<()> {
    let mut command = crate::env::cargo();
    command.stdin(Stdio::null());
    command.stdout(Stdio::null());
    command.stderr(Stdio::inherit());

    if is_test {
        command.arg("test");
        command.arg("--no-run");
    } else {
        command.arg("build");
        command.arg("--lib");
    }

    command.arg("--package");
    command.arg(package_name);

    if let Some(user_manifest_path) = user_manifest_path.as_ref() {
        command.arg("--manifest-path");
        command.arg(user_manifest_path.as_ref());
    }

    command.args(profile.cargo_args());

    if let Some(log_level) = &log_level {
        command.env("RUST_LOG", log_level);
    }

    if !features_arg.trim().is_empty() {
        command.arg("--features");
        command.arg(features_arg);
    }

    if features.no_default_features {
        command.arg("--no-default-features");
    }

    if features.all_features {
        command.arg("--all-features");
    }

    for arg in flags.split_ascii_whitespace() {
        command.arg(arg);
    }

    if let Some(target) = target {
        command.arg("--target");
        command.arg(target);
    }

    let command_str = format!("{command:?}");
    eprintln!(
        "{} for SQL generation with features `{}`",
        "    Building".bold().green(),
        features_arg,
    );

    tracing::debug!(command = %command_str, "Running");
    let cargo_output =
        command.output().wrap_err_with(|| format!("failed to spawn cargo: {command_str}"))?;
    tracing::trace!(status_code = %cargo_output.status, command = %command_str, "Finished");

    if !cargo_output.status.success() {
        // We explicitly do not want to return a spantraced error here.
        std::process::exit(1)
    }

    Ok(())
}

fn compute_codegen(
    control_file_path: impl AsRef<Path>,
    package_manifest_path: impl AsRef<Path>,
    symbols: &[String],
    lib_name: &str,
    path: Option<String>,
    dot: Option<String>,
) -> eyre::Result<String> {
    use proc_macro2::{Ident, Span, TokenStream};
    let lib_name_ident = Ident::new(lib_name, Span::call_site());

    let inputs = {
        let control_file_path = control_file_path
            .as_ref()
            .to_str()
            .expect(".control file filename should be valid UTF8");
        let mut out = quote::quote! {
            // call the marker.  Primarily this ensures that rustc will actually link to the library
            // during the "pgrx_embed" build initiated by `cargo-pgrx schema` generation
            #lib_name_ident::__pgrx_marker();

            let mut entities = Vec::new();
            let control_file_path = std::path::PathBuf::from(#control_file_path);
            let control_file = ::pgrx::pgrx_sql_entity_graph::ControlFile::try_from(control_file_path).expect(".control file should properly formatted");
            let control_file_entity = ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::ExtensionRoot(control_file);

            entities.push(control_file_entity);
        };
        for name in symbols.iter() {
            let name_ident = Ident::new(name, Span::call_site());
            out.extend(quote::quote! {
                unsafe extern "Rust" {
                    fn #name_ident() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity;
                }
                let entity = unsafe { #name_ident() };
                entities.push(entity);
            });
        }
        out
    };
    let build = {
        let versioned_so = get_property(&package_manifest_path, "module_pathname")?.is_none();
        quote::quote! {
            let pgrx_sql = ::pgrx::pgrx_sql_entity_graph::PgrxSql::build(
                entities.into_iter(),
                #lib_name.to_string(),
                #versioned_so,
            )
            .expect("SQL generation error");
        }
    };
    let outputs = {
        let mut out = TokenStream::new();
        if let Some(path) = path {
            let writing = "     Writing".bold().green().to_string();
            out.extend(quote::quote! {
                eprintln!("{} SQL entities to {}", #writing, #path);
                pgrx_sql
                    .to_file(#path)
                    .expect(&format!("Could not write SQL to {}", #path));
            });
        } else {
            let writing = "     Writing".bold().green().to_string();
            out.extend(quote::quote! {
                eprintln!("{} SQL entities to {}", #writing, "/dev/stdout",);
                pgrx_sql
                    .write(&mut std::io::stdout())
                    .expect("Could not write SQL to stdout");
            });
        }
        if let Some(dot) = dot {
            out.extend(quote::quote! {
                pgrx_sql
                    .to_dot(#dot)
                    .expect("Could not write Graphviz DOT");
            });
        }
        out
    };
    Ok(quote::quote! {
        pub fn main() {
            #inputs
            #build
            #outputs
        }
    }
    .to_string())
}

fn second_build(
    user_manifest_path: Option<&impl AsRef<Path>>,
    features: &clap_cargo::Features,
    log_level: Option<String>,
    features_arg: &str,
    flags: &str,
    embed_path: impl AsRef<Path>,
    package_name: &str,
) -> eyre::Result<()> {
    let mut command = crate::env::cargo();
    command.stdin(Stdio::null());
    command.stdout(Stdio::null());
    command.stderr(Stdio::inherit());

    // We do pass cfg to the binary and do not pass cfg to dependencies to avoid recompilation
    // The only cargo command respecting our need is `cargo rustc`
    command.arg("rustc");
    command.arg("--bin");
    command.arg(format!("pgrx_embed_{package_name}"));

    command.arg("--package");
    command.arg(package_name);

    if let Some(user_manifest_path) = user_manifest_path.as_ref() {
        command.arg("--manifest-path");
        command.arg(user_manifest_path.as_ref());
    }

    if let Some(log_level) = &log_level {
        command.env("RUST_LOG", log_level);
    }

    if !features_arg.trim().is_empty() {
        command.arg("--features");
        command.arg(features_arg);
    }

    if features.no_default_features {
        command.arg("--no-default-features");
    }

    if features.all_features {
        command.arg("--all-features");
    }

    for arg in flags.split_ascii_whitespace() {
        command.arg(arg);
    }

    command.arg("--");

    command.args(["--cfg", "pgrx_embed"]);

    command.env("PGRX_EMBED", embed_path.as_ref());

    let command_str = format!("{command:?}");
    eprintln!(
        "{} {}, in debug mode, for SQL generation with features {}",
        "  Rebuilding".bold().green(),
        "pgrx_embed".cyan(),
        features_arg.cyan(),
    );

    tracing::debug!(command = %command_str, "Running");
    let cargo_output =
        command.output().wrap_err_with(|| format!("failed to spawn cargo: {command_str}"))?;
    tracing::trace!(status_code = %cargo_output.status, command = %command_str, "Finished");

    if !cargo_output.status.success() {
        // We explicitly do not want to return a spantraced error here.
        std::process::exit(1)
    }

    Ok(())
}

fn compute_sql(package_name: &str, manifest: &Manifest) -> eyre::Result<()> {
    let mut bin = get_target_dir()?;
    bin.push("debug"); // pgrx_embed_ is always compiled in debug mode
    bin.push(format!("pgrx_embed_{package_name}"));

    let mut command = std::process::Command::new(bin);
    command.stdin(Stdio::inherit());
    command.stdout(Stdio::inherit());
    command.stderr(Stdio::inherit());

    // pass the package version through as an environment variable
    let cargo_pkg_version = std::env::var("CARGO_PKG_VERSION").unwrap_or_else(|_| {
        manifest.package_version().expect("`CARGO_PKG_VERSION` should be set, and barring that, `Cargo.toml` should have a package version property")
    });
    command.env("CARGO_PKG_VERSION", cargo_pkg_version);

    let command_str = format!("{command:?}");
    tracing::debug!(command = %command_str, "Running");
    let embed_output =
        command.output().wrap_err_with(|| format!("failed to spawn pgrx_embed: {command_str}"))?;
    tracing::trace!(status_code = %embed_output.status, command = %command_str, "Finished");

    if !embed_output.status.success() {
        // We do not want to return a spantraced error here, to
        // (speculative:) reduce the likelihood of emitting errors twice
        std::process::exit(1)
    }

    Ok(())
}

fn parse_object(data: &[u8]) -> object::Result<object::File> {
    let kind = object::FileKind::parse(data)?;

    match kind {
        object::FileKind::MachOFat32 => {
            let arch = std::env::consts::ARCH;

            match slice_arch32(data, arch) {
                Some(slice) => parse_object(slice),
                None => {
                    panic!("Failed to slice architecture '{arch}' from universal binary.")
                }
            }
        }
        _ => object::File::parse(data),
    }
}

fn slice_arch32<'a>(data: &'a [u8], arch: &str) -> Option<&'a [u8]> {
    use object::read::macho::FatArch;
    use object::Architecture;
    let target = match arch {
        "x86" => Architecture::I386,
        "x86_64" => Architecture::X86_64,
        "arm" => Architecture::Arm,
        "aarch64" => Architecture::Aarch64,
        "mips" => Architecture::Mips,
        "powerpc" => Architecture::PowerPc,
        "powerpc64" => Architecture::PowerPc64,
        _ => Architecture::Unknown,
    };

    let candidates = MachOFatFile32::parse(data).ok()?;
    let architecture = candidates.arches().iter().find(|a| a.architecture() == target)?;

    architecture.data(data).ok()
}

#[cfg(test)]
mod tests {
    use crate::command::schema::*;
    use pgrx_pg_config::PgConfigSelector;

    #[test]
    fn test_parse_managed_postmasters() {
        let pgrx = Pgrx::from_config().unwrap();
        let mut results = pgrx
            .iter(PgConfigSelector::All)
            .map(|pg_config| {
                let fixture_path = pg_config.unwrap().postmaster_path().unwrap();
                let bin = std::fs::read(fixture_path).unwrap();

                parse_object(&bin).is_ok()
            })
            .peekable();

        assert!(results.peek().is_some());
        assert!(results.all(|r| r));
    }

    #[test]
    fn test_parse_universal_binary_slice() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        let slice = slice_arch32(&bin, "aarch64")
            .expect("Failed to slice architecture 'aarch64' from universal binary.");
        assert!(parse_object(slice).is_ok());
    }

    #[test]
    fn test_slice_unknown_architecture() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        assert!(slice_arch32(&bin, "foo").is_none());
    }
}
