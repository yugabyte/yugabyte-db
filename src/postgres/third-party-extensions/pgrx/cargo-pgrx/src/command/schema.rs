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
use crate::cargo::{self, Cargo, CargoProfile};
use crate::command::get::{find_control_file, get_property};
use crate::manifest::{get_package_manifest, pg_config_and_version};
use crate::object_utils::schema_section_data;
use cargo_toml::Manifest;
use eyre::WrapErr;
use owo_colors::OwoColorize;
use pgrx_pg_config::cargo::PgrxManifestExt;
use pgrx_pg_config::{Pgrx, get_target_dir, is_supported_major_version};
use pgrx_sql_entity_graph::section::decode_entities;
use pgrx_sql_entity_graph::{ControlFile, PgrxSql, SqlGraphEntity};
use std::path::{Path, PathBuf};

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
    /// Positional arguments.
    ///
    /// The first may be a PostgreSQL version label (`pg13`..`pg18`); every
    /// remaining value is an SQL item name to emit (functions, types,
    /// enums, operators, aggregates, triggers, schemas, extension_sql
    /// blocks). Only those items and their transitive dependencies are
    /// emitted, in install order, and `'MODULE_PATHNAME'` is substituted
    /// with `'$libdir/<lib_name>'` so the output can be replayed directly.
    /// Names containing `::` are matched as Rust paths to disambiguate.
    args: Vec<String>,
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
    /// Don't emit `ALTER EXTENSION ... ADD ...` statements when extracting
    /// specific items. By default, item mode emits ALTER EXTENSION so the
    /// output can be piped into a running database and attached to the
    /// already-installed extension.
    #[clap(long)]
    no_alter_extension: bool,
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

        let (pg_version, items) = split_positional_args(&self.args);

        let pgrx = Pgrx::from_config()?;
        let (package_manifest, package_manifest_path) = get_package_manifest(
            &self.features,
            self.package.as_deref(),
            self.manifest_path.as_deref(),
        )?;
        // This does meaningful mutation, unfortunately
        let (_pg_config, _pg_version) = pg_config_and_version(
            &pgrx,
            &package_manifest,
            pg_version,
            Some(&mut self.features),
            true,
        )?;

        let profile = CargoProfile::from_flags(
            self.profile.as_deref(),
            if self.release { CargoProfile::Release } else { CargoProfile::Dev },
        )?;

        let attach = !self.no_alter_extension;
        generate_schema(
            self.manifest_path.as_deref(),
            self.package.as_deref(),
            &package_manifest_path,
            &profile,
            self.test,
            &self.features,
            self.target.as_deref(),
            self.out.as_deref(),
            self.dot.as_deref(),
            log_level,
            self.skip_build,
            items,
            attach,
            &mut vec![],
        )
    }
}

/// Split the schema command's positional arguments into an optional
/// `pgXX` version label and an optional list of SQL item names.
///
/// If the first argument parses as a supported PostgreSQL major version it
/// is consumed as `pg_version`; everything after it (or everything, if
/// there is no version) flows through as item names. `None` items means
/// the caller supplied no names at all — as distinct from an empty slice.
fn split_positional_args(args: &[String]) -> (Option<String>, Option<&[String]>) {
    let (pg_version, rest) = if let Some((first, rest)) = args.split_first()
        && let Some(major) = first.strip_prefix("pg")
        && let Ok(major) = major.parse::<u16>()
        && is_supported_major_version(major)
    {
        (Some(first.clone()), rest)
    } else {
        (None, args)
    };
    (pg_version, (!rest.is_empty()).then_some(rest))
}

#[tracing::instrument(level = "error", skip_all, fields(
    profile = ?profile,
    test = is_test,
    path = path.map(|path| tracing::field::display(path.display())),
    dot,
    features = ?features.features,
))]
pub(crate) fn generate_schema_for_cli(
    user_manifest_path: Option<&Path>,
    user_package: Option<&str>,
    package_manifest_path: &Path,
    profile: &CargoProfile,
    is_test: bool,
    features: &clap_cargo::Features,
    target: Option<&str>,
    path: Option<&Path>,
    dot: Option<&Path>,
    log_level: Option<String>,
    skip_build: bool,
    items: Option<&[String]>,
    attach: bool,
    output_tracking: &mut Vec<PathBuf>,
) -> eyre::Result<()> {
    let manifest = Manifest::from_path(package_manifest_path)?;
    let features_arg = features.features.join(" ");

    let package_name = if let Some(user_package) = user_package {
        user_package.to_owned()
    } else {
        manifest.package_name()?
    };

    let cargo = Cargo::default()
        .package(package_name)
        .std_streams([cargo::Stdio::Null, cargo::Stdio::Null, cargo::Stdio::Inherit])
        .manifest_path(user_manifest_path.map(|p| p.to_owned()))
        .log_level(log_level)
        .features(features.clone());

    if !skip_build {
        // NB:  The only path where this happens is via the command line using `cargo pgrx schema`
        first_build(cargo.clone(), profile, is_test, &features_arg, target)?;
    };
    generate_schema_implicit(
        package_manifest_path,
        profile,
        target,
        path,
        dot,
        items,
        attach,
        output_tracking,
        manifest,
    )
}
pub(crate) use generate_schema_for_cli as generate_schema;

pub(crate) fn generate_schema_implicit(
    package_manifest_path: &Path,
    profile: &CargoProfile,
    target: Option<&str>,
    path: Option<&Path>,
    dot: Option<&Path>,
    items: Option<&[String]>,
    attach: bool,
    output_tracking: &mut Vec<PathBuf>,
    manifest: cargo_toml::Manifest,
) -> eyre::Result<()> {
    let (control_file_path, extname) = find_control_file(package_manifest_path)?;
    let lib_name = manifest.lib_name()?;
    let lib_filename = manifest.lib_filename()?;
    let versioned_so = get_property(package_manifest_path, "module_pathname")?.is_none();
    let extension_version = manifest.package_version()?;

    if let Some(out_path) = path {
        if let Some(parent) = out_path.parent() {
            std::fs::create_dir_all(parent).wrap_err("Could not create parent directory")?;
        }
        output_tracking.push(out_path.to_path_buf());
    }

    if let Some(dot_path) = dot.as_ref() {
        tracing::info!(dot = %dot_path.display(), "Writing Graphviz DOT");
    }

    let lib_so_data = load_section_data(profile, &lib_filename, target)?;
    let section_entities = decode_section_entities(&lib_so_data)?;
    report_entity_counts(&section_entities);

    let mut entities = Vec::new();
    entities.push(SqlGraphEntity::ExtensionRoot(ControlFile::from_path_with_cargo_version(
        &control_file_path,
        &extension_version,
    )?));
    entities.extend(section_entities);

    let pgrx_sql = PgrxSql::build(entities.into_iter(), lib_name.to_string(), versioned_so)
        .wrap_err("SQL generation error")?;

    if let Some(items) = items {
        let extension_name = attach.then_some(extname.as_str());
        let sliced = pgrx_sql
            .to_sql_for_items(items, &lib_name, extension_name)
            .wrap_err("Could not generate SQL for requested items")?;
        if let Some(path) = path {
            eprintln!(
                "{} SQL for {} item(s) to {}",
                "     Writing".bold().green(),
                items.len(),
                path.display()
            );
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(path, sliced)
                .wrap_err_with(|| format!("Could not write SQL to {}", path.display()))?;
        } else {
            eprintln!(
                "{} SQL for {} item(s) to {}",
                "     Writing".bold().green(),
                items.len(),
                "/dev/stdout"
            );
            use std::io::Write as _;
            std::io::stdout()
                .write_all(sliced.as_bytes())
                .wrap_err("Could not write SQL to stdout")?;
        }
    } else if let Some(path) = path {
        eprintln!("{} SQL entities to {}", "     Writing".bold().green(), path.display());
        pgrx_sql
            .to_file(path)
            .wrap_err_with(|| format!("Could not write SQL to {}", path.display()))?;
    } else {
        eprintln!("{} SQL entities to /dev/stdout", "     Writing".bold().green());
        pgrx_sql.write(&mut std::io::stdout()).wrap_err("Could not write SQL to stdout")?;
    }

    if let Some(dot) = dot {
        pgrx_sql
            .to_dot(dot)
            .wrap_err_with(|| format!("Could not write Graphviz DOT to {}", dot.display()))?;
    }

    Ok(())
}

fn load_section_data(
    profile: &CargoProfile,
    lib_filename: &str,
    target: Option<&str>,
) -> eyre::Result<Vec<u8>> {
    let mut lib_so = get_target_dir()?;
    if let Some(target) = target {
        lib_so.push(target);
    }
    lib_so.push(profile.target_subdir());
    lib_so.push(lib_filename);

    std::fs::read(&lib_so).wrap_err("couldn't read extension shared object")
}

fn decode_section_entities<'a>(lib_so_data: &'a [u8]) -> eyre::Result<Vec<SqlGraphEntity<'a>>> {
    let section = schema_section_data(lib_so_data)?.ok_or_else(|| {
        eyre::eyre!(
            "no embedded pgrx schema section found; expected `.pgrxsc` on ELF/PE or `__DATA,__pgrxsc` on Mach-O. the artifact may have been built with an incompatible pgrx, stripped incorrectly, or selected from the wrong architecture slice",
        )
    })?;
    decode_entities(section).wrap_err("couldn't decode pgrx schema section")
}

fn report_entity_counts(entities: &[SqlGraphEntity<'_>]) {
    let mut seen_schemas = Vec::new();
    let mut num_funcs = 0_usize;
    let mut num_triggers = 0_usize;
    let mut num_types = 0_usize;
    let mut num_enums = 0_usize;
    let mut num_sqls = 0_usize;
    let mut num_ords = 0_usize;
    let mut num_hashes = 0_usize;
    let mut num_aggregates = 0_usize;
    for entity in entities {
        match entity {
            SqlGraphEntity::Schema(schema) => seen_schemas.push(schema.name),
            SqlGraphEntity::Function(_) => num_funcs += 1,
            SqlGraphEntity::Trigger(_) => num_triggers += 1,
            SqlGraphEntity::Type(_) => num_types += 1,
            SqlGraphEntity::Enum(_) => num_enums += 1,
            SqlGraphEntity::CustomSql(_) => num_sqls += 1,
            SqlGraphEntity::Ord(_) => num_ords += 1,
            SqlGraphEntity::Hash(_) => num_hashes += 1,
            SqlGraphEntity::Aggregate(_) => num_aggregates += 1,
            SqlGraphEntity::BuiltinType(_) | SqlGraphEntity::ExtensionRoot(_) => (),
        }
    }

    eprintln!(
        "{} {} SQL entities: {} schemas ({} unique), {} functions, {} types, {} enums, {} sqls, {} ords, {} hashes, {} aggregates, {} triggers",
        "  Discovered".bold().green(),
        entities.len().to_string().bold().cyan(),
        seen_schemas.len().to_string().bold().cyan(),
        seen_schemas
            .iter()
            .collect::<std::collections::HashSet<_>>()
            .len()
            .to_string()
            .bold()
            .cyan(),
        num_funcs.to_string().bold().cyan(),
        num_types.to_string().bold().cyan(),
        num_enums.to_string().bold().cyan(),
        num_sqls.to_string().bold().cyan(),
        num_ords.to_string().bold().cyan(),
        num_hashes.to_string().bold().cyan(),
        num_aggregates.to_string().bold().cyan(),
        num_triggers.to_string().bold().cyan(),
    );
}

fn first_build(
    cargo: Cargo,
    profile: &CargoProfile,
    is_test: bool,
    features_arg: &str,
    target: Option<&str>,
) -> eyre::Result<()> {
    let cargo = if is_test {
        cargo.subcommand("test").flag("--no-run")
    } else {
        cargo.subcommand("build").flag("--lib")
    };

    let mut cargo = cargo.profile(profile.clone()).target(target.map(|t| t.to_owned()));

    // `cargo build --lib` only ever links the cdylib, so it's safe to apply
    // the `.pgrxsc` retention workaround. `cargo test --no-run` also links
    // the test binary, which rustc needs `--gc-sections` to prune undefined
    // Postgres `extern "C"` references from; applying the workaround there
    // surfaces those as linker errors. See `pgrx_cdylib_config_args`.
    if !is_test {
        cargo = cargo.with_pgrx_cdylib_workaround();
    }

    let mut command = cargo.into_command();

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
        std::process::exit(1)
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{decode_section_entities, split_positional_args};

    fn strs(args: &[&str]) -> Vec<String> {
        args.iter().map(|s| (*s).to_owned()).collect()
    }

    #[test]
    fn test_missing_schema_section_errors() {
        let root_path = env!("CARGO_MANIFEST_DIR");
        let fixture_path = format!("{root_path}/tests/fixtures/macos-universal-binary");
        let bin = std::fs::read(fixture_path).unwrap();

        let error = decode_section_entities(&bin).expect_err("missing section");
        assert!(error.to_string().contains("no embedded pgrx schema section found"));
    }

    #[test]
    fn empty_args_yield_no_version_and_no_items() {
        let args = strs(&[]);
        let (pg, items) = split_positional_args(&args);
        assert!(pg.is_none());
        assert!(items.is_none());
    }

    #[test]
    fn version_alone_is_captured() {
        let args = strs(&["pg18"]);
        let (pg, items) = split_positional_args(&args);
        assert_eq!(pg.as_deref(), Some("pg18"));
        assert!(items.is_none());
    }

    #[test]
    fn version_followed_by_items() {
        let args = strs(&["pg18", "sum_vec", "MyType", "==="]);
        let (pg, items) = split_positional_args(&args);
        assert_eq!(pg.as_deref(), Some("pg18"));
        assert_eq!(items, Some(&["sum_vec".to_owned(), "MyType".to_owned(), "===".to_owned()][..]));
    }

    #[test]
    fn items_only_without_version() {
        let args = strs(&["sum_vec", "MyType", "==="]);
        let (pg, items) = split_positional_args(&args);
        assert!(pg.is_none());
        assert_eq!(items, Some(&["sum_vec".to_owned(), "MyType".to_owned(), "===".to_owned()][..]));
    }

    #[test]
    fn first_arg_that_looks_like_version_but_isnt_is_an_item() {
        let args = strs(&["pgfoo", "sum_vec"]);
        let (pg, items) = split_positional_args(&args);
        assert!(pg.is_none());
        assert_eq!(items, Some(&["pgfoo".to_owned(), "sum_vec".to_owned()][..]));
    }
}
