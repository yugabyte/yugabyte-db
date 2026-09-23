//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!

Rust to SQL mapping support.

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/

use eyre::eyre;
use petgraph::Direction;
use petgraph::dot::Dot;
use petgraph::graph::NodeIndex;
use petgraph::stable_graph::StableGraph;
use petgraph::visit::EdgeRef;
use std::collections::{BTreeMap, HashMap, HashSet, VecDeque};
use std::fmt::Debug;
use std::path::Path;

use crate::aggregate::entity::PgAggregateEntity;
use crate::control_file::ControlFile;
use crate::extension_sql::SqlDeclared;
use crate::extension_sql::entity::{ExtensionSqlEntity, SqlDeclaredEntity};
use crate::metadata::TypeOrigin;
use crate::pg_extern::entity::PgExternEntity;
use crate::pg_trigger::entity::PgTriggerEntity;
use crate::positioning_ref::PositioningRef;
use crate::postgres_enum::entity::PostgresEnumEntity;
use crate::postgres_hash::entity::PostgresHashEntity;
use crate::postgres_ord::entity::PostgresOrdEntity;
use crate::postgres_type::entity::PostgresTypeEntity;
use crate::schema::entity::SchemaEntity;
use crate::to_sql::ToSql;
use crate::type_keyed;
use crate::{SqlGraphEntity, SqlGraphIdentifier, UsedTypeEntity};

use super::{PgExternReturnEntity, PgExternReturnEntityIteratedItem};

#[derive(Debug, Clone, Copy, PartialEq, PartialOrd, Eq, Ord)]
pub enum SqlGraphRequires {
    By,
    ByArg,
    ByReturn,
}

/// A generator for SQL.
///
/// Consumes a base mapping of types (typically `pgrx::DEFAULT_TYPEID_SQL_MAPPING`), a
/// [`ControlFile`], and collections of each SQL entity.
///
/// During construction, a Directed Acyclic Graph is formed out the dependencies. For example,
/// an item `detect_dog(x: &[u8]) -> animals::Dog` would have have a relationship with
/// `animals::Dog`.
///
/// Typically, [`PgrxSql`] types are constructed in a `pgrx::pg_binary_magic!()` call in a binary
/// out of entities collected during a `pgrx::pg_module_magic!()` call in a library.
#[derive(Debug, Clone)]
pub struct PgrxSql<'a> {
    pub control: ControlFile,
    pub graph: StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    pub graph_root: NodeIndex,
    pub graph_bootstrap: Option<NodeIndex>,
    pub graph_finalize: Option<NodeIndex>,
    pub schemas: HashMap<SchemaEntity<'a>, NodeIndex>,
    pub extension_sqls: HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    pub externs: HashMap<PgExternEntity<'a>, NodeIndex>,
    pub types: HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    pub builtin_types: HashMap<String, NodeIndex>,
    pub enums: HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    pub ords: HashMap<PostgresOrdEntity<'a>, NodeIndex>,
    pub hashes: HashMap<PostgresHashEntity<'a>, NodeIndex>,
    pub aggregates: HashMap<PgAggregateEntity<'a>, NodeIndex>,
    pub triggers: HashMap<PgTriggerEntity<'a>, NodeIndex>,
    pub extension_name: String,
    pub versioned_so: bool,
}

impl<'a> PgrxSql<'a> {
    pub fn build(
        entities: impl Iterator<Item = SqlGraphEntity<'a>>,
        extension_name: String,
        versioned_so: bool,
    ) -> eyre::Result<Self> {
        let mut graph = StableGraph::new();

        let mut entities = entities.collect::<Vec<_>>();
        entities.sort();
        // Split up things into their specific types:
        let mut control: Option<ControlFile> = None;
        let mut schemas: Vec<SchemaEntity<'a>> = Vec::default();
        let mut extension_sqls: Vec<ExtensionSqlEntity<'a>> = Vec::default();
        let mut externs: Vec<PgExternEntity<'a>> = Vec::default();
        let mut types: Vec<PostgresTypeEntity<'a>> = Vec::default();
        let mut enums: Vec<PostgresEnumEntity<'a>> = Vec::default();
        let mut ords: Vec<PostgresOrdEntity<'a>> = Vec::default();
        let mut hashes: Vec<PostgresHashEntity<'a>> = Vec::default();
        let mut aggregates: Vec<PgAggregateEntity<'a>> = Vec::default();
        let mut triggers: Vec<PgTriggerEntity<'a>> = Vec::default();
        for entity in entities {
            match entity {
                SqlGraphEntity::ExtensionRoot(input_control) => {
                    control = Some(input_control);
                }
                SqlGraphEntity::Schema(input_schema) => {
                    schemas.push(input_schema);
                }
                SqlGraphEntity::CustomSql(input_sql) => {
                    extension_sqls.push(input_sql);
                }
                SqlGraphEntity::Function(input_function) => {
                    externs.push(input_function);
                }
                SqlGraphEntity::Type(input_type) => {
                    types.push(input_type);
                }
                SqlGraphEntity::BuiltinType(_) => (),
                SqlGraphEntity::Enum(input_enum) => {
                    enums.push(input_enum);
                }
                SqlGraphEntity::Ord(input_ord) => {
                    ords.push(input_ord);
                }
                SqlGraphEntity::Hash(input_hash) => {
                    hashes.push(input_hash);
                }
                SqlGraphEntity::Aggregate(input_aggregate) => {
                    aggregates.push(input_aggregate);
                }
                SqlGraphEntity::Trigger(input_trigger) => {
                    triggers.push(input_trigger);
                }
            }
        }

        let control: ControlFile = control.expect("No control file found");
        let root = graph.add_node(SqlGraphEntity::ExtensionRoot(control.clone()));

        // The initial build phase.
        //
        // Notably, we do not set non-root edges here. We do that in a second step. This is
        // primarily because externs, types, operators, and the like tend to intertwine. If we tried
        // to do it here, we'd find ourselves trying to create edges to non-existing entities.

        // Both of these must be unique, so we can only hold one.
        // Populate nodes, but don't build edges until we know if there is a bootstrap/finalize.
        let (mapped_extension_sqls, bootstrap, finalize) =
            initialize_extension_sqls(&mut graph, root, extension_sqls)?;
        let mapped_schemas = initialize_schemas(&mut graph, bootstrap, finalize, schemas)?;
        let mapped_enums = initialize_enums(&mut graph, root, bootstrap, finalize, enums)?;
        let mapped_types = initialize_types(&mut graph, root, bootstrap, finalize, types)?;
        ensure_unique_type_targets(&mapped_types, &mapped_enums, &mapped_extension_sqls)?;
        let (mapped_externs, mut mapped_builtin_types) = initialize_externs(
            &mut graph,
            root,
            bootstrap,
            finalize,
            externs,
            &mapped_types,
            &mapped_enums,
            &mapped_extension_sqls,
        )?;
        let mapped_ords = initialize_ords(&mut graph, root, bootstrap, finalize, ords)?;
        let mapped_hashes = initialize_hashes(&mut graph, root, bootstrap, finalize, hashes)?;
        let mapped_aggregates = initialize_aggregates(
            &mut graph,
            root,
            bootstrap,
            finalize,
            aggregates,
            &mut mapped_builtin_types,
            &mapped_enums,
            &mapped_types,
            &mapped_extension_sqls,
        )?;
        let mapped_triggers = initialize_triggers(&mut graph, root, bootstrap, finalize, triggers)?;

        // Now we can circle back and build up the edge sets.
        connect_schemas(&mut graph, &mapped_schemas, root);
        connect_extension_sqls(
            &mut graph,
            &mapped_extension_sqls,
            &mapped_schemas,
            &mapped_types,
            &mapped_enums,
            &mapped_externs,
            &mapped_triggers,
        )?;
        connect_enums(&mut graph, &mapped_enums, &mapped_schemas);
        connect_types(&mut graph, &mapped_types, &mapped_schemas, &mapped_externs)?;
        connect_externs(
            &mut graph,
            &mapped_externs,
            &mapped_hashes,
            &mapped_schemas,
            &mapped_types,
            &mapped_enums,
            &mapped_builtin_types,
            &mapped_extension_sqls,
            &mapped_triggers,
        )?;
        connect_ords(
            &mut graph,
            &mapped_ords,
            &mapped_schemas,
            &mapped_types,
            &mapped_enums,
            &mapped_externs,
        );
        connect_hashes(
            &mut graph,
            &mapped_hashes,
            &mapped_schemas,
            &mapped_types,
            &mapped_enums,
            &mapped_externs,
        );
        connect_aggregates(
            &mut graph,
            &mapped_aggregates,
            &mapped_schemas,
            &mapped_types,
            &mapped_enums,
            &mapped_builtin_types,
            &mapped_externs,
            &mapped_extension_sqls,
        )?;
        connect_triggers(&mut graph, &mapped_triggers, &mapped_schemas);

        let this = Self {
            control,
            schemas: mapped_schemas,
            extension_sqls: mapped_extension_sqls,
            externs: mapped_externs,
            types: mapped_types,
            builtin_types: mapped_builtin_types,
            enums: mapped_enums,
            ords: mapped_ords,
            hashes: mapped_hashes,
            aggregates: mapped_aggregates,
            triggers: mapped_triggers,
            graph,
            graph_root: root,
            graph_bootstrap: bootstrap,
            graph_finalize: finalize,
            extension_name,
            versioned_so,
        };
        Ok(this)
    }

    // NOTE: this signature is demanded by the codegen we embed via cargo-pgrx
    pub fn to_file(&self, file: impl AsRef<Path> + Debug) -> eyre::Result<()> {
        use std::fs::{File, create_dir_all};
        use std::io::Write;
        let generated = self.to_sql()?;
        let path = Path::new(file.as_ref());

        let parent = path.parent();
        if let Some(parent) = parent {
            create_dir_all(parent)?;
        }
        let mut out = File::create(path)?;
        write!(out, "{generated}")?;
        Ok(())
    }

    pub fn write(&self, out: &mut impl std::io::Write) -> eyre::Result<()> {
        let generated = self.to_sql()?;

        #[cfg(feature = "syntax-highlighting")]
        {
            use std::io::{IsTerminal, stdout};
            if stdout().is_terminal() {
                self.write_highlighted(out, &generated)?;
            } else {
                write!(*out, "{}", generated)?;
            }
        }

        #[cfg(not(feature = "syntax-highlighting"))]
        {
            write!(*out, "{generated}")?;
        }

        Ok(())
    }

    #[cfg(feature = "syntax-highlighting")]
    fn write_highlighted(&self, out: &mut dyn std::io::Write, generated: &str) -> eyre::Result<()> {
        use eyre::WrapErr as _;
        use owo_colors::{OwoColorize, XtermColors};
        use syntect::easy::HighlightLines;
        use syntect::highlighting::{Style, ThemeSet};
        use syntect::parsing::SyntaxSet;
        use syntect::util::LinesWithEndings;
        let ps = SyntaxSet::load_defaults_newlines();
        let theme_bytes = include_str!("../assets/ansi.tmTheme").as_bytes();
        let mut theme_reader = std::io::Cursor::new(theme_bytes);
        let theme = ThemeSet::load_from_reader(&mut theme_reader)
            .wrap_err("Couldn't parse theme for SQL highlighting, try piping to a file")?;

        if let Some(syntax) = ps.find_syntax_by_extension("sql") {
            let mut h = HighlightLines::new(syntax, &theme);
            for line in LinesWithEndings::from(&generated) {
                let ranges: Vec<(Style, &str)> = h.highlight_line(line, &ps)?;
                // Concept from https://github.com/sharkdp/bat/blob/1b030dc03b906aa345f44b8266bffeea77d763fe/src/terminal.rs#L6
                for (style, content) in ranges {
                    if style.foreground.a == 0x01 {
                        write!(*out, "{}", content)?;
                    } else {
                        write!(*out, "{}", content.color(XtermColors::from(style.foreground.r)))?;
                    }
                }
                write!(*out, "\x1b[0m")?;
            }
        } else {
            write!(*out, "{}", generated)?;
        }
        Ok(())
    }

    // NOTE: this signature is demanded by the codegen we embed via cargo-pgrx
    pub fn to_dot(&self, file: impl AsRef<Path> + Debug) -> eyre::Result<()> {
        use std::fs::{File, create_dir_all};
        use std::io::Write;
        let generated = Dot::with_attr_getters(
            &self.graph,
            &[petgraph::dot::Config::EdgeNoLabel, petgraph::dot::Config::NodeNoLabel],
            &|_graph, edge| {
                match edge.weight() {
                    SqlGraphRequires::By => r#"color = "gray""#,
                    SqlGraphRequires::ByArg => r#"color = "black""#,
                    SqlGraphRequires::ByReturn => r#"dir = "back", color = "black""#,
                }
                .to_owned()
            },
            &|_graph, (_index, node)| {
                let dot_id = node.dot_identifier();
                match node {
                    // Colors derived from https://www.schemecolor.com/touch-of-creativity.php
                    SqlGraphEntity::Schema(_item) => {
                        format!("label = \"{dot_id}\", weight = 6, shape = \"tab\"")
                    }
                    SqlGraphEntity::Function(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#ADC7C6\", weight = 4, shape = \"box\"",
                    ),
                    SqlGraphEntity::Type(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#AE9BBD\", weight = 5, shape = \"oval\"",
                    ),
                    SqlGraphEntity::BuiltinType(_item) => {
                        format!("label = \"{dot_id}\", shape = \"plain\"")
                    }
                    SqlGraphEntity::Enum(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#C9A7C8\", weight = 5, shape = \"oval\""
                    ),
                    SqlGraphEntity::Ord(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#FFCFD3\", weight = 5, shape = \"diamond\""
                    ),
                    SqlGraphEntity::Hash(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#FFE4E0\", weight = 5, shape = \"diamond\""
                    ),
                    SqlGraphEntity::Aggregate(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#FFE4E0\", weight = 5, shape = \"diamond\""
                    ),
                    SqlGraphEntity::Trigger(_item) => format!(
                        "label = \"{dot_id}\", penwidth = 0, style = \"filled\", fillcolor = \"#FFE4E0\", weight = 5, shape = \"diamond\""
                    ),
                    SqlGraphEntity::CustomSql(_item) => {
                        format!("label = \"{dot_id}\", weight = 3, shape = \"signature\"")
                    }
                    SqlGraphEntity::ExtensionRoot(_item) => {
                        format!("label = \"{dot_id}\", shape = \"cylinder\"")
                    }
                }
            },
        );
        let path = Path::new(file.as_ref());

        let parent = path.parent();
        if let Some(parent) = parent {
            create_dir_all(parent)?;
        }
        let mut out = File::create(path)?;
        write!(out, "{generated:?}")?;
        Ok(())
    }

    pub fn schema_alias_of(&self, item_index: &NodeIndex) -> Option<String> {
        self.graph
            .neighbors_undirected(*item_index)
            .flat_map(|neighbor_index| match &self.graph[neighbor_index] {
                SqlGraphEntity::Schema(s) => Some(String::from(s.name)),
                SqlGraphEntity::ExtensionRoot(_control) => None,
                _ => None,
            })
            .next()
    }

    pub fn schema_prefix_for(&self, target: &NodeIndex) -> String {
        self.schema_alias_of(target).map(|v| (v + ".").to_string()).unwrap_or_default()
    }

    pub fn find_type_dependency(
        &self,
        owner: &NodeIndex,
        ty: &dyn crate::TypeIdentifiable,
    ) -> Option<NodeIndex> {
        self.graph
            .neighbors_undirected(*owner)
            .find(|neighbor| self.graph[*neighbor].type_matches(ty))
    }

    pub fn schema_prefix_for_used_type(
        &self,
        owner: &NodeIndex,
        slot: &str,
        used_ty: &UsedTypeEntity<'_>,
    ) -> eyre::Result<String> {
        if !used_ty.needs_type_resolution() {
            return Ok(String::new());
        }

        let graph_index = self
            .find_type_dependency(owner, used_ty)
            .ok_or_else(|| eyre!("Could not find {slot} in graph. Got: {used_ty:?}"))?;
        Ok(self.schema_prefix_for(&graph_index))
    }

    pub fn to_sql(&self) -> eyre::Result<String> {
        let mut full_sql = String::new();

        // NB:  A properly we'd *like* to maintain is that the schema generator outputs
        // consistent results from run-to-run when there are no changes to the schema.
        // This is to improve change detection using simple tools like `diff`.
        //
        // Historically, we used [`petgraph::algo:toposort`] but its ordering is not at all
        // consistent.
        //
        // [`petgraph::algo::tarjan_scc`] appears to be consistent, although it's not exactly
        // clear if this is due to an implementation detail or specifics of the algorithm itself.
        // (I, eeeebbbbrrrr, am not a graph theory expert)
        //
        // In any event, if in the future schema generation stops being consistent, this is the
        // place to look.
        //
        // We have no tests around this as it's really just a property we'd like to have, and
        // it does seem ensuring it is a bit of black magic.
        for nodes in petgraph::algo::tarjan_scc(&self.graph).iter().rev() {
            let mut inner_sql = Vec::with_capacity(nodes.len());

            for node in self.connected_component_emit_order(nodes) {
                let step = &self.graph[node];
                let sql = step.to_sql(self)?;

                let trimmed = sql.trim();
                if !trimmed.is_empty() {
                    inner_sql.push(format!("{trimmed}\n"))
                }
            }

            if !inner_sql.is_empty() {
                full_sql.push_str("/* <begin connected objects> */\n");
                full_sql.push_str(&inner_sql.join("\n\n"));
                full_sql.push_str("/* </end connected objects> */\n\n");
            }
        }

        Ok(full_sql)
    }

    fn connected_component_emit_order(&self, nodes: &[NodeIndex]) -> Vec<NodeIndex> {
        if nodes.len() <= 1 {
            return nodes.to_vec();
        }

        // When a connected component contains a cycle, user-authored `requires = [...]`
        // edges are the strongest ordering signal we have. Type-resolution edges may still
        // point back into the declaration that ultimately creates the type, such as shell-type
        // bootstrap patterns for manual `extension_sql!()` types.
        let mut explicit_dependents = HashMap::<NodeIndex, Vec<NodeIndex>>::new();
        let mut remaining_explicit_dependencies = HashMap::<NodeIndex, usize>::new();
        let mut has_explicit_edges = false;

        for &node in nodes {
            explicit_dependents.insert(node, Vec::new());
            remaining_explicit_dependencies.insert(node, 0);
        }

        for &node in nodes {
            for edge in self.graph.edges(node) {
                if edge.weight() != &SqlGraphRequires::By {
                    continue;
                }

                let dependent = edge.target();
                if !remaining_explicit_dependencies.contains_key(&dependent) {
                    continue;
                }

                has_explicit_edges = true;
                explicit_dependents
                    .get_mut(&node)
                    .expect("component members should be initialized")
                    .push(dependent);
                *remaining_explicit_dependencies
                    .get_mut(&dependent)
                    .expect("component members should be initialized") += 1;
            }
        }

        if !has_explicit_edges {
            return nodes.to_vec();
        }

        let mut ready = remaining_explicit_dependencies
            .iter()
            .filter_map(|(node, count)| (*count == 0).then_some(*node))
            .collect::<Vec<_>>();
        let mut ordered = Vec::with_capacity(nodes.len());

        while !ready.is_empty() {
            ready.sort_unstable_by(|left, right| {
                self.graph[*left]
                    .cmp(&self.graph[*right])
                    .then_with(|| left.index().cmp(&right.index()))
            });
            let next = ready.remove(0);
            ordered.push(next);

            if let Some(dependents) = explicit_dependents.get(&next) {
                for dependent in dependents {
                    let remaining = remaining_explicit_dependencies
                        .get_mut(dependent)
                        .expect("component members should be initialized");
                    *remaining -= 1;
                    if *remaining == 0 {
                        ready.push(*dependent);
                    }
                }
            }
        }

        if ordered.len() == nodes.len() { ordered } else { nodes.to_vec() }
    }

    pub fn has_sql_declared_entity(&self, identifier: &SqlDeclared) -> Option<&SqlDeclaredEntity> {
        self.extension_sqls.iter().find_map(|(item, _index)| {
            item.creates
                .iter()
                .find(|create_entity| create_entity.has_sql_declared_entity(identifier))
        })
    }

    pub fn get_module_pathname(&self) -> String {
        if self.versioned_so {
            let extname = &self.extension_name;
            let extver = &self.control.default_version;
            // Note: versioned so-name format must agree with cargo pgrx
            format!("{extname}-{extver}")
        } else {
            String::from("MODULE_PATHNAME")
        }
    }

    pub fn find_matching_fn(&self, name: &str) -> Option<&PgExternEntity<'a>> {
        self.externs.keys().find(|key| key.full_path.ends_with(name))
    }

    /// Resolve a single user-supplied item name to one graph node.
    ///
    /// A match is any entity whose SQL-visible name, Rust path, or operator
    /// symbol equals `name` exactly. A `::`-bearing argument is treated as a
    /// Rust path (matched only against `full_path`). Ambiguous hits are a
    /// hard error.
    pub fn resolve_item(&self, name: &str) -> eyre::Result<NodeIndex> {
        let by_path = name.contains("::");
        let mut matches: Vec<(NodeIndex, String)> = Vec::new();

        for (entity, &idx) in &self.externs {
            let fn_hit = if by_path {
                entity.full_path == name
            } else {
                entity.name == name || entity.unaliased_name == name
            };
            if fn_hit {
                matches.push((idx, format!("function `{}`", entity.full_path)));
            }
            if !by_path
                && let Some(op) = &entity.operator
                && op.opname == Some(name)
                && !matches.iter().any(|(existing, _)| *existing == idx)
            {
                matches.push((idx, format!("operator `{}` on `{}`", name, entity.full_path)));
            }
        }

        for (entity, &idx) in &self.types {
            let hit = if by_path { entity.full_path == name } else { entity.name == name };
            if hit {
                matches.push((idx, format!("type `{}`", entity.full_path)));
            }
        }

        for (entity, &idx) in &self.enums {
            let hit = if by_path { entity.full_path == name } else { entity.name == name };
            if hit {
                matches.push((idx, format!("enum `{}`", entity.full_path)));
            }
        }

        for (entity, &idx) in &self.aggregates {
            let hit = if by_path { entity.full_path == name } else { entity.name == name };
            if hit {
                matches.push((idx, format!("aggregate `{}`", entity.full_path)));
            }
        }

        for (entity, &idx) in &self.triggers {
            let hit = if by_path { entity.full_path == name } else { entity.function_name == name };
            if hit {
                matches.push((idx, format!("trigger `{}`", entity.full_path)));
            }
        }

        for (entity, &idx) in &self.extension_sqls {
            if !by_path && entity.name == name {
                matches.push((idx, format!("extension_sql `{}`", entity.name)));
                continue;
            }
            for declared in &entity.creates {
                let declared_name = match declared {
                    SqlDeclaredEntity::Type(data) | SqlDeclaredEntity::Enum(data) => {
                        data.name.as_str()
                    }
                    SqlDeclaredEntity::Function(data) => data.name.as_str(),
                };
                if declared_name == name {
                    matches.push((
                        idx,
                        format!("extension_sql `{}` (declares `{declared_name}`)", entity.name),
                    ));
                    break;
                }
            }
        }

        for (entity, &idx) in &self.schemas {
            if !by_path && entity.name == name {
                matches.push((idx, format!("schema `{}`", entity.name)));
            }
        }

        match matches.len() {
            0 => Err(eyre!("no SQL entity matches `{name}`")),
            1 => Ok(matches.remove(0).0),
            _ => {
                let labels = matches.iter().map(|(_, l)| l.as_str()).collect::<Vec<_>>().join(", ");
                Err(eyre!(
                    "`{name}` is ambiguous; matched: {labels}. Disambiguate with a `::`-qualified Rust path."
                ))
            }
        }
    }

    /// Emit SQL for the given item names plus all transitive dependencies, in
    /// dependency order, and substitute `'MODULE_PATHNAME'` with
    /// `'$libdir/<lib_name>'` so the output can be replayed directly into a
    /// database.
    ///
    /// When `extension_name` is `Some(name)`, the emitted slice is wrapped in
    /// `BEGIN;`/`COMMIT;` and each created object is followed by an
    /// `ALTER EXTENSION "<name>" ADD …` clause so that piping the output into
    /// a database where the extension is already installed attaches the new
    /// objects to the extension. When `None`, the pre-feature behavior is
    /// used (no transaction wrapping, no ADD clauses).
    ///
    /// Warnings (e.g. for `extension_sql!()` blocks without `creates = [...]`)
    /// are written to stderr. Use `emit_slice_with_warnings` directly if you
    /// need to capture them.
    pub fn to_sql_for_items(
        &self,
        item_names: &[String],
        lib_name: &str,
        extension_name: Option<&str>,
    ) -> eyre::Result<String> {
        self.emit_slice_with_warnings(item_names, lib_name, extension_name, |msg| {
            eprintln!("{msg}");
        })
    }

    /// Core of [`Self::to_sql_for_items`]. Takes a warning sink so tests
    /// (and future non-stderr callers) can observe the diagnostics that
    /// would otherwise go to stderr.
    pub(crate) fn emit_slice_with_warnings<W: FnMut(String)>(
        &self,
        item_names: &[String],
        lib_name: &str,
        extension_name: Option<&str>,
        warn: W,
    ) -> eyre::Result<String> {
        let mut targets = Vec::with_capacity(item_names.len());
        for name in item_names {
            targets.push(self.resolve_item(name)?);
        }
        self.emit_slice_from_nodes(&targets, lib_name, extension_name, warn)
    }

    /// Same as [`Self::emit_slice_with_warnings`] but takes already-resolved
    /// node indices. Used by tests that need to target entities whose
    /// resolution is ambiguous or not supported by [`Self::resolve_item`]
    /// (e.g. `Ord` and `Hash` derives).
    pub(crate) fn emit_slice_from_nodes<W: FnMut(String)>(
        &self,
        targets: &[NodeIndex],
        lib_name: &str,
        extension_name: Option<&str>,
        mut warn: W,
    ) -> eyre::Result<String> {
        let keep = self.collect_transitive_deps(targets);

        let mut body = String::new();
        for nodes in petgraph::algo::tarjan_scc(&self.graph).iter().rev() {
            let ordered = self.connected_component_emit_order(nodes);
            let mut block = Vec::new();

            for node in ordered {
                if !keep.contains(&node) {
                    continue;
                }
                let ent = &self.graph[node];

                // The ExtensionRoot's CREATE-phase output is a trivial comment
                // block that reads "auto generated by pgrx". Inside a slice
                // aimed at an already-installed extension it would be strange
                // and confusing, so skip it.
                if matches!(ent, SqlGraphEntity::ExtensionRoot(_)) {
                    continue;
                }

                let create_sql = ent.to_sql(self)?;
                let create_sql = create_sql.trim();

                let mut piece = String::new();
                if !create_sql.is_empty() {
                    piece.push_str(create_sql);
                    piece.push('\n');
                }

                if let Some(ext) = extension_name {
                    match self.render_alter_extension_for_node(node, ext)? {
                        Some(alter_sql) => {
                            piece.push_str(&alter_sql);
                            if !alter_sql.ends_with('\n') {
                                piece.push('\n');
                            }
                        }
                        None => {
                            if let SqlGraphEntity::CustomSql(c) = ent
                                && c.creates.is_empty()
                            {
                                warn(format!(
                                    "warning: extension_sql block at {}:{} does not declare `creates = [...]`; its objects won't be attached to the extension automatically",
                                    c.file, c.line,
                                ));
                            }
                        }
                    }
                }

                if !piece.is_empty() {
                    block.push(piece);
                }
            }

            if !block.is_empty() {
                body.push_str("/* <begin connected objects> */\n");
                body.push_str(&block.join("\n"));
                body.push_str("/* </end connected objects> */\n\n");
            }
        }

        let replacement = format!("'$libdir/{lib_name}'");
        let body = body.replace("'MODULE_PATHNAME'", &replacement);

        Ok(match extension_name {
            Some(_) => format!("BEGIN;\n\n{body}\nCOMMIT;\n"),
            None => body,
        })
    }

    /// Produce the `ALTER EXTENSION "<ext>" ADD …;` clauses for `node`, or
    /// `Ok(None)` when the node is not an extension-attachable object
    /// (builtin type, extension root, or a free-form `extension_sql!()`
    /// block that didn't declare `creates = [...]`).
    fn render_alter_extension_for_node(
        &self,
        node: NodeIndex,
        extension_name: &str,
    ) -> eyre::Result<Option<String>> {
        let ent = &self.graph[node];
        let ext = extension_name;

        match ent {
            SqlGraphEntity::Function(f) => {
                let schema = f
                    .schema
                    .map(|s| format!("{s}."))
                    .unwrap_or_else(|| self.schema_prefix_for(&node));
                let argtypes = crate::pg_extern::entity::render_function_argtypes(self, node, f)?;
                let mut out = format!(
                    "ALTER EXTENSION \"{ext}\" ADD FUNCTION {schema}\"{name}\"({argtypes});",
                    name = f.name
                );

                if let Some(op) = &f.operator
                    && let Some(opname) = op.opname
                {
                    let left = f
                        .fn_args
                        .first()
                        .ok_or_else(|| eyre!("operator `{}` missing left argument", f.name))?;
                    let right = f
                        .fn_args
                        .get(1)
                        .ok_or_else(|| eyre!("operator `{}` missing right argument", f.name))?;
                    let left_sql = crate::pg_extern::entity::render_used_type_sql(
                        self,
                        node,
                        "operator left argument",
                        &left.used_ty,
                    )?;
                    let right_sql = crate::pg_extern::entity::render_used_type_sql(
                        self,
                        node,
                        "operator right argument",
                        &right.used_ty,
                    )?;
                    out.push('\n');
                    out.push_str(&format!(
                        "ALTER EXTENSION \"{ext}\" ADD OPERATOR {schema}{opname}({left_sql}, {right_sql});"
                    ));
                }

                if f.cast.is_some() {
                    let source = f
                        .fn_args
                        .first()
                        .ok_or_else(|| eyre!("cast `{}` missing source argument", f.name))?;
                    let source_sql = crate::pg_extern::entity::render_used_type_sql(
                        self,
                        node,
                        "cast source type",
                        &source.used_ty,
                    )?;
                    let target_sql =
                        crate::pg_extern::entity::render_function_return_type(self, node, f)?;
                    out.push('\n');
                    out.push_str(&format!(
                        "ALTER EXTENSION \"{ext}\" ADD CAST ({source_sql} AS {target_sql});"
                    ));
                }

                Ok(Some(out))
            }
            SqlGraphEntity::Type(t) => {
                let schema = self.schema_prefix_for(&node);
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD TYPE {schema}{name};",
                    name = t.name
                )))
            }
            SqlGraphEntity::Enum(e) => {
                let schema = self.schema_prefix_for(&node);
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD TYPE {schema}{name};",
                    name = e.name
                )))
            }
            SqlGraphEntity::Aggregate(a) => {
                let schema = self.schema_prefix_for(&node);
                let argtypes = crate::aggregate::entity::render_aggregate_argtypes(self, node, a)?;
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD AGGREGATE {schema}\"{name}\"{argtypes};",
                    name = a.name
                )))
            }
            SqlGraphEntity::Trigger(t) => {
                let schema = self.schema_prefix_for(&node);
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD FUNCTION {schema}\"{name}\"();",
                    name = t.function_name
                )))
            }
            SqlGraphEntity::Ord(o) => {
                // Unqualified names: matches `PostgresOrdEntity::to_sql`, which
                // also emits `{name}_btree_ops` without a schema prefix.
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD OPERATOR FAMILY {name}_btree_ops USING btree;\n\
                     ALTER EXTENSION \"{ext}\" ADD OPERATOR CLASS {name}_btree_ops USING btree;",
                    name = o.name
                )))
            }
            SqlGraphEntity::Hash(h) => {
                // Same unqualified-name rationale as Ord.
                Ok(Some(format!(
                    "ALTER EXTENSION \"{ext}\" ADD OPERATOR FAMILY {name}_hash_ops USING hash;\n\
                     ALTER EXTENSION \"{ext}\" ADD OPERATOR CLASS {name}_hash_ops USING hash;",
                    name = h.name
                )))
            }
            SqlGraphEntity::Schema(s) => {
                if matches!(s.name, "public" | "pg_catalog") {
                    return Ok(None);
                }
                Ok(Some(format!("ALTER EXTENSION \"{ext}\" ADD SCHEMA {name};", name = s.name)))
            }
            SqlGraphEntity::CustomSql(c) => {
                if c.creates.is_empty() {
                    return Ok(None);
                }
                let mut out = String::new();
                for (idx, declared) in c.creates.iter().enumerate() {
                    if idx > 0 {
                        out.push('\n');
                    }
                    match declared {
                        SqlDeclaredEntity::Type(data) => {
                            out.push_str(&format!(
                                "ALTER EXTENSION \"{ext}\" ADD TYPE {};",
                                data.sql
                            ));
                        }
                        SqlDeclaredEntity::Enum(data) => {
                            out.push_str(&format!(
                                "ALTER EXTENSION \"{ext}\" ADD TYPE {};",
                                data.sql
                            ));
                        }
                        SqlDeclaredEntity::Function(data) => {
                            out.push_str(&format!(
                                "ALTER EXTENSION \"{ext}\" ADD FUNCTION {};",
                                data.sql
                            ));
                        }
                    }
                }
                Ok(Some(out))
            }
            SqlGraphEntity::BuiltinType(_) | SqlGraphEntity::ExtensionRoot(_) => Ok(None),
        }
    }

    /// Collect every node reachable from `targets` by walking edges backward
    /// (i.e. every dependency that must exist before the targets can be
    /// created). The returned set always contains the targets themselves.
    fn collect_transitive_deps(&self, targets: &[NodeIndex]) -> HashSet<NodeIndex> {
        let mut visited = HashSet::new();
        let mut queue = VecDeque::new();
        for &t in targets {
            if visited.insert(t) {
                queue.push_back(t);
            }
        }
        while let Some(node) = queue.pop_front() {
            for predecessor in self.graph.neighbors_directed(node, Direction::Incoming) {
                if visited.insert(predecessor) {
                    queue.push_back(predecessor);
                }
            }
        }
        visited
    }
}

fn build_base_edges<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    index: NodeIndex,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
) {
    graph.add_edge(root, index, SqlGraphRequires::By);
    if let Some(bootstrap) = bootstrap {
        graph.add_edge(bootstrap, index, SqlGraphRequires::By);
    }
    if let Some(finalize) = finalize {
        graph.add_edge(index, finalize, SqlGraphRequires::By);
    }
}

#[allow(clippy::type_complexity)]
fn initialize_extension_sqls<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    extension_sqls: Vec<ExtensionSqlEntity<'a>>,
) -> eyre::Result<(HashMap<ExtensionSqlEntity<'a>, NodeIndex>, Option<NodeIndex>, Option<NodeIndex>)>
{
    let mut bootstrap = None;
    let mut finalize = None;
    let mut mapped_extension_sqls = HashMap::default();
    for item in extension_sqls {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_extension_sqls.insert(item.clone(), index);

        if item.bootstrap {
            if let Some(existing_index) = bootstrap {
                let existing: &SqlGraphEntity = &graph[existing_index];
                return Err(eyre!(
                    "Cannot have multiple `extension_sql!()` with `bootstrap` positioning, found `{}`, other was `{}`",
                    item.rust_identifier(),
                    existing.rust_identifier(),
                ));
            }
            bootstrap = Some(index)
        }
        if item.finalize {
            if let Some(existing_index) = finalize {
                let existing: &SqlGraphEntity = &graph[existing_index];
                return Err(eyre!(
                    "Cannot have multiple `extension_sql!()` with `finalize` positioning, found `{}`, other was `{}`",
                    item.rust_identifier(),
                    existing.rust_identifier(),
                ));
            }
            finalize = Some(index)
        }
    }
    for (item, index) in &mapped_extension_sqls {
        graph.add_edge(root, *index, SqlGraphRequires::By);
        if !item.bootstrap
            && let Some(bootstrap) = bootstrap
        {
            graph.add_edge(bootstrap, *index, SqlGraphRequires::By);
        }
        if !item.finalize
            && let Some(finalize) = finalize
        {
            graph.add_edge(*index, finalize, SqlGraphRequires::By);
        }
    }
    Ok((mapped_extension_sqls, bootstrap, finalize))
}

/// A best effort attempt to find the related [`NodeIndex`] for some [`PositioningRef`].
pub fn find_positioning_ref_target<'a, 'b>(
    positioning_ref: &'b PositioningRef,
    types: &'b HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &'b HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    externs: &'b HashMap<PgExternEntity<'a>, NodeIndex>,
    schemas: &'b HashMap<SchemaEntity<'a>, NodeIndex>,
    extension_sqls: &'b HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    triggers: &'b HashMap<PgTriggerEntity<'a>, NodeIndex>,
) -> Option<&'b NodeIndex> {
    match positioning_ref {
        PositioningRef::FullPath(path) => {
            // The best we can do here is a fuzzy search.
            let segments = path.split("::").collect::<Vec<_>>();
            let last_segment = segments.last().expect("Expected at least one segment.");
            let rest = &segments[..segments.len() - 1];
            let module_path = rest.join("::");

            for (other, other_index) in types {
                if *last_segment == other.name && other.module_path.ends_with(&module_path) {
                    return Some(other_index);
                }
            }
            for (other, other_index) in enums {
                if last_segment == &other.name && other.module_path.ends_with(&module_path) {
                    return Some(other_index);
                }
            }
            for (other, other_index) in externs {
                if *last_segment == other.unaliased_name
                    && other.module_path.ends_with(&module_path)
                {
                    return Some(other_index);
                }
            }
            for (other, other_index) in schemas {
                if other.module_path.ends_with(path) {
                    return Some(other_index);
                }
            }

            for (other, other_index) in triggers {
                if last_segment == &other.function_name && other.module_path.ends_with(&module_path)
                {
                    return Some(other_index);
                }
            }
        }
        PositioningRef::Name(name) => {
            for (other, other_index) in extension_sqls {
                if other.name == name {
                    return Some(other_index);
                }
            }
        }
    };
    None
}

fn connect_extension_sqls<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
    triggers: &HashMap<PgTriggerEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    for (item, &index) in extension_sqls {
        make_schema_connection(
            graph,
            "Extension SQL",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );

        for requires in &item.requires {
            if let Some(target) = find_positioning_ref_target(
                requires,
                types,
                enums,
                externs,
                schemas,
                extension_sqls,
                triggers,
            ) {
                graph.add_edge(*target, index, SqlGraphRequires::By);
            } else {
                return Err(eyre!(
                    "Could not find `requires` target of `{}`{}: {}",
                    item.rust_identifier(),
                    match (item.file(), item.line()) {
                        (Some(file), Some(line)) => format!(" ({file}:{line})"),
                        _ => "".to_string(),
                    },
                    match requires {
                        PositioningRef::FullPath(path) => path.to_string(),
                        PositioningRef::Name(name) => format!(r#""{name}""#),
                    },
                ));
            }
        }
    }
    Ok(())
}

fn initialize_schemas<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    schemas: Vec<SchemaEntity<'a>>,
) -> eyre::Result<HashMap<SchemaEntity<'a>, NodeIndex>> {
    let mut mapped_schemas = HashMap::default();
    for item in schemas {
        let entity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_schemas.insert(item, index);
        if let Some(bootstrap) = bootstrap {
            graph.add_edge(bootstrap, index, SqlGraphRequires::By);
        }
        if let Some(finalize) = finalize {
            graph.add_edge(index, finalize, SqlGraphRequires::By);
        }
    }
    Ok(mapped_schemas)
}

fn connect_schemas<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    root: NodeIndex,
) {
    for index in schemas.values().copied() {
        graph.add_edge(root, index, SqlGraphRequires::By);
    }
}

fn initialize_enums<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    enums: Vec<PostgresEnumEntity<'a>>,
) -> eyre::Result<HashMap<PostgresEnumEntity<'a>, NodeIndex>> {
    let mut mapped_enums = HashMap::default();
    for item in enums {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_enums.insert(item, index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_enums)
}

fn connect_enums<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
) {
    for (item, &index) in enums {
        make_schema_connection(
            graph,
            "Enum",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );
    }
}

fn initialize_types<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    types: Vec<PostgresTypeEntity<'a>>,
) -> eyre::Result<HashMap<PostgresTypeEntity<'a>, NodeIndex>> {
    let mut mapped_types = HashMap::default();
    for item in types {
        let entity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_types.insert(item, index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_types)
}

fn connect_types<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    for (item, &index) in types {
        make_schema_connection(
            graph,
            "Type",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );

        make_extern_connection(
            graph,
            "Type",
            index,
            &item.rust_identifier(),
            &resolve_function_path(item.module_path, item.in_fn_path),
            externs,
        )?;
        make_extern_connection(
            graph,
            "Type",
            index,
            &item.rust_identifier(),
            &resolve_function_path(item.module_path, item.out_fn_path),
            externs,
        )?;
        if let Some(path) = item.receive_fn_path {
            make_extern_connection(
                graph,
                "Type",
                index,
                &item.rust_identifier(),
                &resolve_function_path(item.module_path, path),
                externs,
            )?;
        }
        if let Some(path) = item.send_fn_path {
            make_extern_connection(
                graph,
                "Type",
                index,
                &item.rust_identifier(),
                &resolve_function_path(item.module_path, path),
                externs,
            )?;
        }
    }
    Ok(())
}

fn initialize_externs<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    externs: Vec<PgExternEntity<'a>>,
    mapped_types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    mapped_enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    mapped_extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> eyre::Result<(HashMap<PgExternEntity<'a>, NodeIndex>, HashMap<String, NodeIndex>)> {
    let mut mapped_externs = HashMap::default();
    let mut mapped_builtin_types = HashMap::default();
    for item in externs {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity.clone());
        mapped_externs.insert(item.clone(), index);
        build_base_edges(graph, index, root, bootstrap, finalize);

        for arg in &item.fn_args {
            if !arg.used_ty.emits_argument_sql() || !arg.used_ty.needs_type_resolution() {
                continue;
            }
            let slot = format!("argument `{}`", arg.pattern);
            let (type_ident, type_origin) = arg
                .used_ty
                .resolution()
                .expect("SQL-visible extern arguments should carry resolution metadata");
            initialize_resolved_type(
                graph,
                &mut mapped_builtin_types,
                type_ident,
                type_origin,
                mapped_types,
                mapped_enums,
                mapped_extension_sqls,
                "Function",
                item.full_path,
                &slot,
                arg.used_ty.full_path,
            )?;
        }

        match &item.fn_return {
            PgExternReturnEntity::None | PgExternReturnEntity::Trigger => (),
            PgExternReturnEntity::Type { ty, .. } | PgExternReturnEntity::SetOf { ty, .. } => {
                if let Some((type_ident, type_origin)) = ty.resolution() {
                    initialize_resolved_type(
                        graph,
                        &mut mapped_builtin_types,
                        type_ident,
                        type_origin,
                        mapped_types,
                        mapped_enums,
                        mapped_extension_sqls,
                        "Function",
                        item.full_path,
                        "return type",
                        ty.full_path,
                    )?;
                }
            }
            PgExternReturnEntity::Iterated { tys: iterated_returns, .. } => {
                for PgExternReturnEntityIteratedItem { ty, .. } in iterated_returns {
                    if let Some((type_ident, type_origin)) = ty.resolution() {
                        initialize_resolved_type(
                            graph,
                            &mut mapped_builtin_types,
                            type_ident,
                            type_origin,
                            mapped_types,
                            mapped_enums,
                            mapped_extension_sqls,
                            "Function",
                            item.full_path,
                            "table return column",
                            ty.full_path,
                        )?;
                    }
                }
            }
        }
    }
    Ok((mapped_externs, mapped_builtin_types))
}

fn connect_externs<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
    hashes: &HashMap<PostgresHashEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    builtin_types: &HashMap<String, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    triggers: &HashMap<PgTriggerEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    for (item, &index) in externs {
        let mut found_schema_declaration = false;
        for extern_attr in &item.extern_attrs {
            match extern_attr {
                crate::ExternArgs::Requires(requirements) => {
                    for requires in requirements {
                        if let Some(target) = find_positioning_ref_target(
                            requires,
                            types,
                            enums,
                            externs,
                            schemas,
                            extension_sqls,
                            triggers,
                        ) {
                            graph.add_edge(*target, index, SqlGraphRequires::By);
                        } else {
                            return Err(eyre!("Could not find `requires` target: {:?}", requires));
                        }
                    }
                }
                crate::ExternArgs::Support(support_fn) => {
                    if let Some(target) = find_positioning_ref_target(
                        support_fn,
                        types,
                        enums,
                        externs,
                        schemas,
                        extension_sqls,
                        triggers,
                    ) {
                        graph.add_edge(*target, index, SqlGraphRequires::By);
                    }
                }
                crate::ExternArgs::Schema(declared_schema_name) => {
                    for (schema, schema_index) in schemas {
                        if schema.name == declared_schema_name {
                            graph.add_edge(*schema_index, index, SqlGraphRequires::By);
                            found_schema_declaration = true;
                        }
                    }
                    if !found_schema_declaration {
                        return Err(eyre!(
                            "Got manual `schema = \"{declared_schema_name}\"` setting, but that schema did not exist."
                        ));
                    }
                }
                _ => (),
            }
        }

        if !found_schema_declaration {
            make_schema_connection(
                graph,
                "Extern",
                index,
                &item.rust_identifier(),
                item.module_path,
                schemas,
            );
        }

        // The hash function must be defined after the {typename}_eq function.
        for (hash_item, &hash_index) in hashes {
            if item.module_path == hash_item.module_path
                && item.name == hash_item.name.to_lowercase() + "_eq"
            {
                graph.add_edge(index, hash_index, SqlGraphRequires::By);
            }
        }

        for arg in &item.fn_args {
            if !arg.used_ty.emits_argument_sql() || !arg.used_ty.needs_type_resolution() {
                continue;
            }
            let slot = format!("argument `{}`", arg.pattern);
            let (type_ident, type_origin) = arg
                .used_ty
                .resolution()
                .expect("SQL-visible extern arguments should carry resolution metadata");
            connect_resolved_type(
                graph,
                index,
                SqlGraphRequires::ByArg,
                type_ident,
                type_origin,
                types,
                enums,
                builtin_types,
                extension_sqls,
                "Function",
                item.full_path,
                &slot,
                arg.used_ty.full_path,
            )?;
        }

        match &item.fn_return {
            PgExternReturnEntity::None | PgExternReturnEntity::Trigger => (),
            PgExternReturnEntity::Type { ty, .. } | PgExternReturnEntity::SetOf { ty, .. } => {
                if let Some((type_ident, type_origin)) = ty.resolution() {
                    connect_resolved_type(
                        graph,
                        index,
                        SqlGraphRequires::ByReturn,
                        type_ident,
                        type_origin,
                        types,
                        enums,
                        builtin_types,
                        extension_sqls,
                        "Function",
                        item.full_path,
                        "return type",
                        ty.full_path,
                    )?;
                }
            }
            PgExternReturnEntity::Iterated { tys: iterated_returns, .. } => {
                for PgExternReturnEntityIteratedItem { ty, .. } in iterated_returns {
                    if let Some((type_ident, type_origin)) = ty.resolution() {
                        connect_resolved_type(
                            graph,
                            index,
                            SqlGraphRequires::ByReturn,
                            type_ident,
                            type_origin,
                            types,
                            enums,
                            builtin_types,
                            extension_sqls,
                            "Function",
                            item.full_path,
                            "table return column",
                            ty.full_path,
                        )?;
                    }
                }
            }
        }
    }
    Ok(())
}

fn initialize_ords<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    ords: Vec<PostgresOrdEntity<'a>>,
) -> eyre::Result<HashMap<PostgresOrdEntity<'a>, NodeIndex>> {
    let mut mapped_ords = HashMap::default();
    for item in ords {
        let entity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_ords.insert(item.clone(), index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_ords)
}

fn connect_ords<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    ords: &HashMap<PostgresOrdEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
) {
    for (item, &index) in ords {
        make_schema_connection(
            graph,
            "Ord",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );

        make_type_or_enum_connection(graph, index, item.type_ident, types, enums);

        // Make PostgresOrdEntities (which will be translated into `CREATE OPERATOR CLASS` statements) depend
        // on the operators which they will reference. For example, a pgrx-defined Postgres type `parakeet`
        // which has `#[derive(PostgresOrd)]` will emit a `parakeet_btree_ops` operator class, which references
        // a definition of a < operator (among others) on the `parakeet` type. This code should ensure that the
        // < operator (along with all the others) is emitted before the `OPERATOR CLASS` itself.

        for (extern_item, &extern_index) in externs {
            let fn_matches = |fn_name| {
                item.module_path == extern_item.module_path && extern_item.name == fn_name
            };
            let cmp_fn_matches = fn_matches(item.cmp_fn_name());
            let lt_fn_matches = fn_matches(item.lt_fn_name());
            let lte_fn_matches = fn_matches(item.le_fn_name());
            let eq_fn_matches = fn_matches(item.eq_fn_name());
            let gt_fn_matches = fn_matches(item.gt_fn_name());
            let gte_fn_matches = fn_matches(item.ge_fn_name());
            if cmp_fn_matches
                || lt_fn_matches
                || lte_fn_matches
                || eq_fn_matches
                || gt_fn_matches
                || gte_fn_matches
            {
                graph.add_edge(extern_index, index, SqlGraphRequires::By);
            }
        }
    }
}

fn initialize_hashes<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    hashes: Vec<PostgresHashEntity<'a>>,
) -> eyre::Result<HashMap<PostgresHashEntity<'a>, NodeIndex>> {
    let mut mapped_hashes = HashMap::default();
    for item in hashes {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity);
        mapped_hashes.insert(item, index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_hashes)
}

fn connect_hashes<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    hashes: &HashMap<PostgresHashEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
) {
    for (item, &index) in hashes {
        make_schema_connection(
            graph,
            "Hash",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );

        make_type_or_enum_connection(graph, index, item.type_ident, types, enums);

        if let Some((_, extern_index)) = externs.iter().find(|(extern_item, _)| {
            item.module_path == extern_item.module_path && extern_item.name == item.fn_name()
        }) {
            graph.add_edge(*extern_index, index, SqlGraphRequires::By);
        }
    }
}

fn initialize_aggregates<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    aggregates: Vec<PgAggregateEntity<'a>>,
    mapped_builtin_types: &mut HashMap<String, NodeIndex>,
    mapped_enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    mapped_types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    mapped_extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> eyre::Result<HashMap<PgAggregateEntity<'a>, NodeIndex>> {
    let mut mapped_aggregates = HashMap::default();
    for item in aggregates {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity);

        for arg in &item.args {
            if !arg.used_ty.needs_type_resolution() {
                continue;
            }
            let slot = aggregate_slot(arg.name, "argument");
            let (type_ident, type_origin) = arg
                .used_ty
                .resolution()
                .expect("aggregate arguments should carry resolution metadata");
            initialize_resolved_type(
                graph,
                mapped_builtin_types,
                type_ident,
                type_origin,
                mapped_types,
                mapped_enums,
                mapped_extension_sqls,
                "Aggregate",
                item.full_path,
                &slot,
                arg.used_ty.full_path,
            )?;
        }

        for arg in item.direct_args.as_ref().unwrap_or(&vec![]) {
            if !arg.used_ty.needs_type_resolution() {
                continue;
            }
            let slot = aggregate_slot(arg.name, "direct argument");
            let (type_ident, type_origin) = arg
                .used_ty
                .resolution()
                .expect("aggregate direct arguments should carry resolution metadata");
            initialize_resolved_type(
                graph,
                mapped_builtin_types,
                type_ident,
                type_origin,
                mapped_types,
                mapped_enums,
                mapped_extension_sqls,
                "Aggregate",
                item.full_path,
                &slot,
                arg.used_ty.full_path,
            )?;
        }

        if let Some((type_ident, type_origin)) = item.stype.used_ty.resolution() {
            initialize_resolved_type(
                graph,
                mapped_builtin_types,
                type_ident,
                type_origin,
                mapped_types,
                mapped_enums,
                mapped_extension_sqls,
                "Aggregate",
                item.full_path,
                "STYPE",
                item.stype.used_ty.full_path,
            )?;
        }

        if let Some(arg) = &item.mstype
            && let Some((type_ident, type_origin)) = arg.resolution()
        {
            initialize_resolved_type(
                graph,
                mapped_builtin_types,
                type_ident,
                type_origin,
                mapped_types,
                mapped_enums,
                mapped_extension_sqls,
                "Aggregate",
                item.full_path,
                "MSTYPE",
                arg.full_path,
            )?;
        }

        mapped_aggregates.insert(item, index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_aggregates)
}

fn connect_aggregate<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    item: &PgAggregateEntity<'a>,
    index: NodeIndex,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    builtin_types: &HashMap<String, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    make_schema_connection(
        graph,
        "Aggregate",
        index,
        &item.rust_identifier(),
        item.module_path,
        schemas,
    );

    for arg in &item.args {
        if !arg.used_ty.needs_type_resolution() {
            continue;
        }
        let slot = aggregate_slot(arg.name, "argument");
        let (type_ident, type_origin) =
            arg.used_ty.resolution().expect("aggregate arguments should carry resolution metadata");
        connect_resolved_type(
            graph,
            index,
            SqlGraphRequires::ByArg,
            type_ident,
            type_origin,
            types,
            enums,
            builtin_types,
            extension_sqls,
            "Aggregate",
            item.full_path,
            &slot,
            arg.used_ty.full_path,
        )?;
    }

    for arg in item.direct_args.as_ref().unwrap_or(&vec![]) {
        if !arg.used_ty.needs_type_resolution() {
            continue;
        }
        let slot = aggregate_slot(arg.name, "direct argument");
        let (type_ident, type_origin) = arg
            .used_ty
            .resolution()
            .expect("aggregate direct arguments should carry resolution metadata");
        connect_resolved_type(
            graph,
            index,
            SqlGraphRequires::ByArg,
            type_ident,
            type_origin,
            types,
            enums,
            builtin_types,
            extension_sqls,
            "Aggregate",
            item.full_path,
            &slot,
            arg.used_ty.full_path,
        )?;
    }

    if let Some(arg) = &item.mstype
        && let Some((type_ident, type_origin)) = arg.resolution()
    {
        connect_resolved_type(
            graph,
            index,
            SqlGraphRequires::ByArg,
            type_ident,
            type_origin,
            types,
            enums,
            builtin_types,
            extension_sqls,
            "Aggregate",
            item.full_path,
            "MSTYPE",
            arg.full_path,
        )?;
    }

    if let Some((type_ident, type_origin)) = item.stype.used_ty.resolution() {
        connect_resolved_type(
            graph,
            index,
            SqlGraphRequires::ByArg,
            type_ident,
            type_origin,
            types,
            enums,
            builtin_types,
            extension_sqls,
            "Aggregate",
            item.full_path,
            "STYPE",
            item.stype.used_ty.full_path,
        )?;
    }

    make_extern_connection(
        graph,
        "Aggregate",
        index,
        &item.rust_identifier(),
        &(item.module_path.to_string() + "::" + item.sfunc),
        externs,
    )?;

    if let Some(value) = item.finalfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.combinefunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.serialfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.deserialfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.msfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.minvfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.mfinalfunc {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    if let Some(value) = item.sortop {
        make_extern_connection(
            graph,
            "Aggregate",
            index,
            &item.rust_identifier(),
            &(item.module_path.to_string() + "::" + value),
            externs,
        )?;
    }
    Ok(())
}

fn connect_aggregates<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    aggregates: &HashMap<PgAggregateEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    builtin_types: &HashMap<String, NodeIndex>,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    for (item, &index) in aggregates {
        connect_aggregate(
            graph,
            item,
            index,
            schemas,
            types,
            enums,
            builtin_types,
            externs,
            extension_sqls,
        )?
    }
    Ok(())
}

fn initialize_triggers<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    triggers: Vec<PgTriggerEntity<'a>>,
) -> eyre::Result<HashMap<PgTriggerEntity<'a>, NodeIndex>> {
    let mut mapped_triggers = HashMap::default();
    for item in triggers {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity);

        mapped_triggers.insert(item, index);
        build_base_edges(graph, index, root, bootstrap, finalize);
    }
    Ok(mapped_triggers)
}

fn connect_triggers<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    triggers: &HashMap<PgTriggerEntity<'a>, NodeIndex>,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
) {
    for (item, &index) in triggers {
        make_schema_connection(
            graph,
            "Trigger",
            index,
            &item.rust_identifier(),
            item.module_path,
            schemas,
        );
    }
}

fn make_schema_connection<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    _kind: &str,
    index: NodeIndex,
    _rust_identifier: &str,
    module_path: &str,
    schemas: &HashMap<SchemaEntity<'a>, NodeIndex>,
) -> bool {
    let mut found = false;
    for (schema_item, &schema_index) in schemas {
        if module_path == schema_item.module_path {
            graph.add_edge(schema_index, index, SqlGraphRequires::By);
            found = true;
            break;
        }
    }
    found
}

fn make_extern_connection<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    _kind: &str,
    index: NodeIndex,
    _rust_identifier: &str,
    full_path: &str,
    externs: &HashMap<PgExternEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    match externs.iter().find(|(extern_item, _)| full_path == extern_item.full_path) {
        Some((_, extern_index)) => {
            graph.add_edge(*extern_index, index, SqlGraphRequires::By);
            Ok(())
        }
        None => Err(eyre!("Did not find connection `{full_path}` in {:#?}", {
            let mut paths = externs.keys().map(|v| v.full_path).collect::<Vec<_>>();
            paths.sort();
            paths
        })),
    }
}

fn resolve_function_path(module_path: &str, path: &str) -> String {
    if path.contains("::") { path.to_string() } else { format!("{module_path}::{path}") }
}

fn aggregate_slot(name: Option<&str>, kind: &str) -> String {
    name.map(|name| format!("{kind} `{name}`")).unwrap_or_else(|| kind.to_string())
}

fn find_type_or_enum<'a>(
    type_ident: &str,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
) -> Option<NodeIndex> {
    types
        .iter()
        .map(type_keyed)
        .chain(enums.iter().map(type_keyed))
        .find(|(ty, _)| ty.matches_type_ident(type_ident))
        .map(|(_, index)| *index)
}

fn find_declared_type_or_enum<'a>(
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    type_ident: &str,
) -> Option<NodeIndex> {
    extension_sqls.iter().find_map(|(item, index)| {
        item.creates
            .iter()
            .any(|declared| declared.matches_type_ident(type_ident))
            .then_some(*index)
    })
}

fn find_graph_type_target<'a>(
    type_ident: &str,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> Option<NodeIndex> {
    find_type_or_enum(type_ident, types, enums)
        .or_else(|| find_declared_type_or_enum(extension_sqls, type_ident))
}

fn ensure_unique_type_targets<'a>(
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
) -> eyre::Result<()> {
    let mut seen = BTreeMap::<String, Vec<String>>::new();

    for item in types.keys() {
        seen.entry(item.type_ident.to_string())
            .or_default()
            .push(format!("type `{}`", item.full_path));
    }

    for item in enums.keys() {
        seen.entry(item.type_ident.to_string())
            .or_default()
            .push(format!("enum `{}`", item.full_path));
    }

    for item in extension_sqls.keys() {
        for declared in &item.creates {
            if let Some(type_ident) = declared.type_ident() {
                seen.entry(type_ident.to_string())
                    .or_default()
                    .push(format!("extension_sql `{}` ({declared})", item.name));
            }
        }
    }

    for locations in seen.values_mut() {
        locations.sort();
    }

    if let Some((type_ident, locations)) =
        seen.into_iter().find(|(_, locations)| locations.len() > 1)
    {
        return Err(eyre!(
            "type ident `{type_ident}` matched multiple SQL entities: {}",
            locations.join(", ")
        ));
    }

    Ok(())
}

fn unresolved_type_ident(
    owner_kind: &str,
    owner_name: &str,
    slot: &str,
    ty_name: &str,
    type_ident: &str,
) -> eyre::Report {
    eyre!(
        "{owner_kind} `{owner_name}` uses `{ty_name}` as {slot}, but type ident `{type_ident}` did not resolve. use `pgrx::pgrx_resolved_type!(T)` together with a matching `#[derive(PostgresType)]`, `#[derive(PostgresEnum)]`, or `extension_sql!(..., creates = [Type(T)]/[Enum(T)])`. for a manual mapping to an existing SQL type, set `TYPE_ORIGIN = TypeOrigin::External`."
    )
}

fn initialize_resolved_type<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    builtin_types: &mut HashMap<String, NodeIndex>,
    type_ident: &str,
    type_origin: TypeOrigin,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    owner_kind: &str,
    owner_name: &str,
    slot: &str,
    ty_name: &str,
) -> eyre::Result<()> {
    if find_graph_type_target(type_ident, types, enums, extension_sqls).is_some() {
        return Ok(());
    }

    if matches!(type_origin, TypeOrigin::External) {
        builtin_types
            .entry(type_ident.to_string())
            .or_insert_with(|| graph.add_node(SqlGraphEntity::BuiltinType(type_ident.to_string())));
        return Ok(());
    }

    Err(unresolved_type_ident(owner_kind, owner_name, slot, ty_name, type_ident))
}

fn connect_resolved_type<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    index: NodeIndex,
    requires: SqlGraphRequires,
    type_ident: &str,
    type_origin: TypeOrigin,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
    builtin_types: &HashMap<String, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity<'a>, NodeIndex>,
    owner_kind: &str,
    owner_name: &str,
    slot: &str,
    ty_name: &str,
) -> eyre::Result<()> {
    if let Some(ty_index) = find_graph_type_target(type_ident, types, enums, extension_sqls) {
        graph.add_edge(ty_index, index, requires);
        return Ok(());
    }

    if let Some(builtin_index) = builtin_types.get(type_ident) {
        graph.add_edge(*builtin_index, index, requires);
        return Ok(());
    }

    if matches!(type_origin, TypeOrigin::External) {
        return Err(eyre!(
            "missing external-type placeholder for type ident `{type_ident}` while connecting {owner_kind} `{owner_name}` {slot}"
        ));
    }

    Err(unresolved_type_ident(owner_kind, owner_name, slot, ty_name, type_ident))
}

fn make_type_or_enum_connection<'a>(
    graph: &mut StableGraph<SqlGraphEntity<'a>, SqlGraphRequires>,
    index: NodeIndex,
    type_ident: &str,
    types: &HashMap<PostgresTypeEntity<'a>, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity<'a>, NodeIndex>,
) -> bool {
    find_type_or_enum(type_ident, types, enums)
        .map(|ty_index| graph.add_edge(ty_index, index, SqlGraphRequires::By))
        .is_some()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::UsedTypeEntity;
    use crate::aggregate::entity::{AggregateTypeEntity, PgAggregateEntity};
    use crate::extension_sql::entity::{
        ExtensionSqlEntity, SqlDeclaredEntity, SqlDeclaredTypeEntityData,
    };
    use crate::extern_args::ExternArgs;
    use crate::metadata::{FunctionMetadataTypeEntity, Returns, SqlArrayMapping, SqlMapping};
    use crate::pg_extern::entity::{
        PgExternArgumentEntity, PgExternEntity, PgExternReturnEntity, PgOperatorEntity,
    };
    use crate::pg_trigger::entity::PgTriggerEntity;
    use crate::postgres_enum::entity::PostgresEnumEntity;
    use crate::postgres_hash::entity::PostgresHashEntity;
    use crate::postgres_ord::entity::PostgresOrdEntity;
    use crate::postgres_type::entity::PostgresTypeEntity;
    use crate::schema::entity::SchemaEntity;
    use crate::to_sql::entity::ToSqlConfigEntity;

    fn control_file() -> ControlFile {
        ControlFile {
            comment: "test".into(),
            default_version: "1.0".into(),
            module_pathname: None,
            relocatable: false,
            superuser: true,
            schema: None,
            trusted: false,
        }
    }

    fn to_sql_config() -> ToSqlConfigEntity<'static> {
        ToSqlConfigEntity { enabled: true, content: None }
    }

    fn used_type(
        full_path: &'static str,
        type_ident: &'static str,
        sql: &'static str,
        type_origin: TypeOrigin,
    ) -> UsedTypeEntity<'static> {
        UsedTypeEntity {
            ty_source: full_path,
            full_path,
            composite_type: None,
            variadic: false,
            default: None,
            optional: false,
            metadata: FunctionMetadataTypeEntity::resolved(
                type_ident,
                type_origin,
                Ok(SqlMapping::literal(sql)),
                Ok(Returns::One(SqlMapping::literal(sql))),
            ),
        }
    }

    fn external_type(
        full_path: &'static str,
        type_ident: &'static str,
        sql: &'static str,
    ) -> UsedTypeEntity<'static> {
        used_type(full_path, type_ident, sql, TypeOrigin::External)
    }

    fn extension_owned_type(
        full_path: &'static str,
        type_ident: &'static str,
        sql: &'static str,
    ) -> UsedTypeEntity<'static> {
        used_type(full_path, type_ident, sql, TypeOrigin::ThisExtension)
    }

    fn function_entity(
        name: &'static str,
        fn_args: Vec<PgExternArgumentEntity<'static>>,
        fn_return: PgExternReturnEntity<'static>,
    ) -> PgExternEntity<'static> {
        PgExternEntity {
            name,
            unaliased_name: name,
            module_path: "tests",
            full_path: Box::leak(format!("tests::{name}").into_boxed_str()),
            fn_args,
            fn_return,
            schema: None,
            file: "test.rs",
            line: 1,
            extern_attrs: vec![],
            search_path: None,
            operator: None,
            cast: None,
            to_sql_config: to_sql_config(),
        }
    }

    fn aggregate_entity(
        name: &'static str,
        args: Vec<AggregateTypeEntity<'static>>,
        stype: UsedTypeEntity<'static>,
        mstype: Option<UsedTypeEntity<'static>>,
    ) -> PgAggregateEntity<'static> {
        PgAggregateEntity {
            full_path: Box::leak(format!("tests::{name}").into_boxed_str()),
            module_path: "tests",
            file: "test.rs",
            line: 1,
            name,
            ordered_set: false,
            args,
            direct_args: None,
            stype: AggregateTypeEntity { used_ty: stype, name: None },
            sfunc: "state_fn",
            finalfunc: None,
            finalfunc_modify: None,
            combinefunc: None,
            serialfunc: None,
            deserialfunc: None,
            initcond: None,
            msfunc: None,
            minvfunc: None,
            mstype,
            mfinalfunc: None,
            mfinalfunc_modify: None,
            minitcond: None,
            sortop: None,
            parallel: None,
            hypothetical: false,
            to_sql_config: to_sql_config(),
        }
    }

    fn declared_type_sql(
        module_path: &'static str,
        full_path: &'static str,
        declaration_name: &'static str,
        name: &'static str,
        type_ident: &'static str,
        sql: &'static str,
    ) -> ExtensionSqlEntity<'static> {
        ExtensionSqlEntity {
            module_path,
            full_path,
            sql: "CREATE TYPE custom_type;",
            file: "test.rs",
            line: 1,
            name: declaration_name,
            bootstrap: false,
            finalize: false,
            requires: vec![],
            creates: vec![SqlDeclaredEntity::Type(SqlDeclaredTypeEntityData {
                sql: sql.into(),
                name: name.into(),
                type_ident: type_ident.into(),
            })],
        }
    }

    fn schema_entity(module_path: &'static str, name: &'static str) -> SchemaEntity<'static> {
        SchemaEntity { module_path, name, file: "test.rs", line: 1 }
    }

    fn type_entity(
        name: &'static str,
        full_path: &'static str,
        type_ident: &'static str,
    ) -> PostgresTypeEntity<'static> {
        PostgresTypeEntity {
            name,
            file: "test.rs",
            line: 1,
            full_path,
            module_path: "tests",
            type_ident,
            in_fn_path: "in_fn",
            out_fn_path: "out_fn",
            receive_fn_path: None,
            send_fn_path: None,
            to_sql_config: to_sql_config(),
            alignment: None,
        }
    }

    fn state_function() -> PgExternEntity<'static> {
        function_entity("state_fn", vec![], PgExternReturnEntity::None)
    }

    #[test]
    fn external_function_type_resolution_succeeds() {
        let manual_text =
            used_type("tests::ManualText", "tests::ManualText", "TEXT", TypeOrigin::External);
        let function = function_entity(
            "manual_text_echo",
            vec![PgExternArgumentEntity { pattern: "value", used_ty: manual_text.clone() }],
            PgExternReturnEntity::Type { ty: manual_text.clone() },
        );

        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .unwrap();

        assert!(sql.builtin_types.contains_key("tests::ManualText"));
    }

    fn skipped_type(full_path: &'static str, type_ident: &'static str) -> UsedTypeEntity<'static> {
        UsedTypeEntity {
            ty_source: full_path,
            full_path,
            composite_type: None,
            variadic: false,
            default: None,
            optional: false,
            metadata: FunctionMetadataTypeEntity::resolved(
                type_ident,
                TypeOrigin::ThisExtension,
                Ok(SqlMapping::Skip),
                Ok(Returns::One(SqlMapping::Skip)),
            ),
        }
    }

    fn explicit_composite_type(name: &'static str) -> UsedTypeEntity<'static> {
        UsedTypeEntity {
            ty_source: "pgrx::heap_tuple::PgHeapTuple<'static, AllocatedByRust>",
            full_path: "pgrx::heap_tuple::PgHeapTuple<'static, AllocatedByRust>",
            composite_type: Some(name),
            variadic: false,
            default: None,
            optional: false,
            metadata: FunctionMetadataTypeEntity::sql_only(
                Ok(SqlMapping::Composite),
                Ok(Returns::One(SqlMapping::Composite)),
            ),
        }
    }

    fn explicit_composite_array_type(name: &'static str) -> UsedTypeEntity<'static> {
        UsedTypeEntity {
            ty_source: "pgrx::heap_tuple::PgHeapTuple<'static, AllocatedByRust>",
            full_path: "pgrx::heap_tuple::PgHeapTuple<'static, AllocatedByRust>",
            composite_type: Some(name),
            variadic: false,
            default: None,
            optional: false,
            metadata: FunctionMetadataTypeEntity::sql_only(
                Ok(SqlMapping::Array(SqlArrayMapping::Composite)),
                Ok(Returns::One(SqlMapping::Array(SqlArrayMapping::Composite))),
            ),
        }
    }

    #[test]
    fn extension_sql_declared_type_orders_before_function_and_aggregate() {
        let custom_type = extension_owned_type("tests::HexInt", "tests::HexInt", "hexint");
        let declared_type = declared_type_sql(
            "tests",
            "tests::concrete_type",
            "concrete_type",
            "tests::HexInt",
            "tests::HexInt",
            "hexint",
        );
        let function = function_entity(
            "takes_hexint",
            vec![PgExternArgumentEntity { pattern: "value", used_ty: custom_type.clone() }],
            PgExternReturnEntity::None,
        );
        let aggregate = aggregate_entity(
            "hexint_accum",
            vec![AggregateTypeEntity { used_ty: custom_type.clone(), name: Some("value") }],
            custom_type.clone(),
            Some(custom_type.clone()),
        );
        let state_fn = state_function();

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::CustomSql(declared_type.clone()),
                SqlGraphEntity::Function(state_fn),
                SqlGraphEntity::Function(function.clone()),
                SqlGraphEntity::Aggregate(aggregate.clone()),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .unwrap();

        let declared_index = sql.extension_sqls[&declared_type];
        let function_index = sql.externs[&function];
        let aggregate_index = sql.aggregates[&aggregate];

        assert!(!sql.builtin_types.contains_key("tests::HexInt"));
        assert!(sql.graph.find_edge(declared_index, function_index).is_some());
        assert!(sql.graph.find_edge(declared_index, aggregate_index).is_some());
    }

    #[test]
    fn declared_type_cycle_prefers_explicit_requirements_with_shell_type() {
        let custom_type = extension_owned_type("tests::HexInt", "tests::HexInt", "hexint");
        let text_type = external_type("alloc::string::String", "alloc::string::String", "text");

        let shell_type = ExtensionSqlEntity {
            module_path: "tests",
            full_path: "tests::shell_type",
            sql: "CREATE TYPE hexint;",
            file: "test.rs",
            line: 1,
            name: "shell_type",
            bootstrap: true,
            finalize: false,
            requires: vec![],
            creates: vec![],
        };

        let mut hexint_in = function_entity(
            "hexint_in",
            vec![],
            PgExternReturnEntity::Type { ty: custom_type.clone() },
        );
        hexint_in.extern_attrs =
            vec![ExternArgs::Requires(vec![PositioningRef::Name("shell_type".into())])];

        let mut hexint_out = function_entity(
            "hexint_out",
            vec![PgExternArgumentEntity { pattern: "value", used_ty: custom_type.clone() }],
            PgExternReturnEntity::Type { ty: text_type },
        );
        hexint_out.extern_attrs =
            vec![ExternArgs::Requires(vec![PositioningRef::Name("shell_type".into())])];

        let mut declared_type = declared_type_sql(
            "tests",
            "tests::concrete_type",
            "concrete_type",
            "tests::HexInt",
            "tests::HexInt",
            "hexint",
        );
        declared_type.sql = "CREATE TYPE hexint (\n    INPUT = hexint_in,\n    OUTPUT = hexint_out,\n    LIKE = int8\n);";
        declared_type.requires = vec![
            PositioningRef::Name("shell_type".into()),
            PositioningRef::FullPath("tests::hexint_in".into()),
            PositioningRef::FullPath("tests::hexint_out".into()),
        ];

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::CustomSql(shell_type),
                SqlGraphEntity::CustomSql(declared_type),
                SqlGraphEntity::Function(hexint_in),
                SqlGraphEntity::Function(hexint_out),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        let shell = sql.find("CREATE TYPE hexint;").unwrap();
        let input = sql.find("-- tests::hexint_in").unwrap();
        let output = sql.find("-- tests::hexint_out").unwrap();
        let concrete = sql.find("CREATE TYPE hexint (\n").unwrap();

        assert!(shell < input);
        assert!(shell < output);
        assert!(input < concrete);
        assert!(output < concrete);
    }

    #[test]
    fn extension_sql_declared_type_in_custom_schema_prefixes_aggregate_state_type() {
        let custom_type = extension_owned_type("tests::HexInt", "tests::HexInt", "hexint");
        let declared_type = declared_type_sql(
            "tests::custom_schema",
            "tests::custom_schema::hexint_sql",
            "hexint_sql",
            "tests::HexInt",
            "tests::HexInt",
            "hexint",
        );
        let aggregate =
            aggregate_entity("hexint_accum", vec![], custom_type.clone(), Some(custom_type));
        let state_fn = state_function();
        let schema = schema_entity("tests::custom_schema", "custom_schema");

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Schema(schema),
                SqlGraphEntity::CustomSql(declared_type),
                SqlGraphEntity::Function(state_fn),
                SqlGraphEntity::Aggregate(aggregate),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        assert!(sql.contains("STYPE = custom_schema.hexint"));
        assert!(sql.contains("MSTYPE = custom_schema.hexint"));
    }

    #[test]
    fn skipped_function_argument_does_not_require_schema_resolution() {
        let function = function_entity(
            "skipped_arg",
            vec![PgExternArgumentEntity {
                pattern: "virtual_arg",
                used_ty: skipped_type("tests::VirtualArg", "tests::VirtualArg"),
            }],
            PgExternReturnEntity::None,
        );

        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        assert!(sql.contains("skipped_arg"));
        assert!(!sql.contains("virtual_arg"));
        assert!(!sql.contains("tests::VirtualArg"));
    }

    #[test]
    fn explicit_composite_type_does_not_require_schema_resolution() {
        let dog = explicit_composite_type("Dog");
        assert!(!dog.needs_type_resolution());

        let function = function_entity("make_dog", vec![], PgExternReturnEntity::Type { ty: dog });

        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        assert!(sql.contains("RETURNS Dog"));
    }

    #[test]
    fn explicit_composite_array_type_does_not_require_schema_resolution() {
        let dog_pack = explicit_composite_array_type("Dog");
        assert!(!dog_pack.needs_type_resolution());

        let function =
            function_entity("make_dog_pack", vec![], PgExternReturnEntity::Type { ty: dog_pack });

        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        assert!(sql.contains("RETURNS Dog[]"));
    }

    #[test]
    fn explicit_composite_array_aggregate_state_does_not_require_schema_resolution() {
        let stype = explicit_composite_array_type("Dog");
        assert!(!stype.needs_type_resolution());
        let mstype = explicit_composite_array_type("Dog");
        assert!(!mstype.needs_type_resolution());

        let aggregate = aggregate_entity("pack_dogs", vec![], stype, Some(mstype));

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(state_function()),
                SqlGraphEntity::Aggregate(aggregate),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .unwrap()
        .to_sql()
        .unwrap();

        assert!(sql.contains("STYPE = Dog[]"));
        assert!(sql.contains("MSTYPE = Dog[]"));
    }

    #[test]
    fn duplicate_type_ident_errors() {
        let left = type_entity("LeftType", "tests::LeftType", "tests::SharedType");
        let right = type_entity("RightType", "tests::RightType", "tests::SharedType");

        let error = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Type(left),
                SqlGraphEntity::Type(right),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("duplicate type idents should fail");

        assert!(error.to_string().contains("tests::SharedType"));
        assert!(error.to_string().contains("tests::LeftType"));
        assert!(error.to_string().contains("tests::RightType"));
    }

    #[test]
    fn unresolved_function_argument_type_ident_errors() {
        let bad_type = extension_owned_type("tests::BadArg", "tests::BadArg", "TEXT");
        let function = function_entity(
            "bad_arg",
            vec![PgExternArgumentEntity { pattern: "value", used_ty: bad_type }],
            PgExternReturnEntity::None,
        );

        let error = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("function argument should fail");

        assert!(error.to_string().contains("Function `tests::bad_arg`"));
        assert!(error.to_string().contains("argument `value`"));
        assert!(error.to_string().contains("tests::BadArg"));
    }

    #[test]
    fn unresolved_function_return_type_ident_errors() {
        let bad_type = extension_owned_type("tests::BadReturn", "tests::BadReturn", "TEXT");
        let function =
            function_entity("bad_return", vec![], PgExternReturnEntity::Type { ty: bad_type });

        let error = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(function)]
                .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("function return should fail");

        assert!(error.to_string().contains("Function `tests::bad_return`"));
        assert!(error.to_string().contains("return type"));
        assert!(error.to_string().contains("tests::BadReturn"));
    }

    #[test]
    fn unresolved_aggregate_argument_type_ident_errors() {
        let aggregate = aggregate_entity(
            "bad_aggregate_arg",
            vec![AggregateTypeEntity {
                used_ty: extension_owned_type("tests::BadArg", "tests::BadArg", "TEXT"),
                name: Some("value"),
            }],
            external_type("tests::State", "tests::State", "TEXT"),
            None,
        );

        let error = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(state_function()),
                SqlGraphEntity::Aggregate(aggregate),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("aggregate argument should fail");

        assert!(error.to_string().contains("Aggregate `tests::bad_aggregate_arg`"));
        assert!(error.to_string().contains("argument `value`"));
        assert!(error.to_string().contains("tests::BadArg"));
    }

    #[test]
    fn unresolved_aggregate_stype_type_ident_errors() {
        let aggregate = aggregate_entity(
            "bad_aggregate_stype",
            vec![],
            extension_owned_type("tests::BadState", "tests::BadState", "TEXT"),
            None,
        );

        let error = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(state_function()),
                SqlGraphEntity::Aggregate(aggregate),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("aggregate stype should fail");

        assert!(error.to_string().contains("Aggregate `tests::bad_aggregate_stype`"));
        assert!(error.to_string().contains("STYPE"));
        assert!(error.to_string().contains("tests::BadState"));
    }

    #[test]
    fn unresolved_aggregate_mstype_type_ident_errors() {
        let aggregate = aggregate_entity(
            "bad_aggregate_mstype",
            vec![],
            external_type("tests::State", "tests::State", "TEXT"),
            Some(extension_owned_type("tests::BadMovingState", "tests::BadMovingState", "TEXT")),
        );

        let error = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(state_function()),
                SqlGraphEntity::Aggregate(aggregate),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .expect_err("aggregate mstype should fail");

        assert!(error.to_string().contains("Aggregate `tests::bad_aggregate_mstype`"));
        assert!(error.to_string().contains("MSTYPE"));
        assert!(error.to_string().contains("tests::BadMovingState"));
    }

    #[test]
    fn to_sql_for_items_emits_only_targets_and_deps_with_lib_substitution() {
        let hexint = extension_owned_type("tests::HexInt", "tests::HexInt", "hexint");
        let declared = declared_type_sql(
            "tests",
            "tests::concrete_type",
            "concrete_type",
            "tests::HexInt",
            "tests::HexInt",
            "hexint",
        );
        let target =
            function_entity("emit_me", vec![], PgExternReturnEntity::Type { ty: hexint.clone() });
        let unused = function_entity(
            "leave_me_out",
            vec![],
            PgExternReturnEntity::Type {
                ty: external_type("alloc::string::String", "alloc::string::String", "text"),
            },
        );

        let pgrx_sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::CustomSql(declared),
                SqlGraphEntity::Function(target),
                SqlGraphEntity::Function(unused),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let sliced = pgrx_sql
            .to_sql_for_items(&["emit_me".into()], "myext", None)
            .expect("slice emission should succeed");

        assert!(sliced.contains("emit_me"), "target function missing:\n{sliced}");
        assert!(sliced.contains("CREATE TYPE custom_type;"), "transitive dep missing:\n{sliced}");
        assert!(!sliced.contains("leave_me_out"), "unrelated function leaked:\n{sliced}");
        assert!(
            sliced.contains("'$libdir/myext'"),
            "MODULE_PATHNAME should be substituted:\n{sliced}"
        );
        assert!(!sliced.contains("'MODULE_PATHNAME'"), "raw placeholder remained:\n{sliced}");
    }

    #[test]
    fn resolve_item_rejects_ambiguous_name_without_path() {
        let dup_a = function_entity("dup_fn", vec![], PgExternReturnEntity::None);
        let mut dup_b = function_entity("dup_fn", vec![], PgExternReturnEntity::None);
        dup_b.module_path = "tests::other";
        dup_b.full_path = "tests::other::dup_fn";

        let pgrx_sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(dup_a),
                SqlGraphEntity::Function(dup_b),
            ]
            .into_iter(),
            "test".into(),
            false,
        )
        .unwrap();

        let err = pgrx_sql.resolve_item("dup_fn").expect_err("ambiguous name should fail");
        let msg = err.to_string();
        assert!(msg.contains("ambiguous"), "expected ambiguity error, got: {msg}");
        assert!(msg.contains("tests::dup_fn"), "got: {msg}");
        assert!(msg.contains("tests::other::dup_fn"), "got: {msg}");

        let unique =
            pgrx_sql.resolve_item("tests::other::dup_fn").expect("qualified path should resolve");
        assert_eq!(pgrx_sql.graph[unique].rust_identifier(), "tests::other::dup_fn");
    }

    fn slice_with_warnings(
        sql: &PgrxSql,
        items: &[String],
        lib_name: &str,
        ext: Option<&str>,
    ) -> (String, Vec<String>) {
        let mut warnings: Vec<String> = Vec::new();
        let out = sql
            .emit_slice_with_warnings(items, lib_name, ext, |msg| warnings.push(msg))
            .expect("slice emission should succeed");
        (out, warnings)
    }

    fn slice_by_nodes(
        sql: &PgrxSql,
        targets: &[NodeIndex],
        lib_name: &str,
        ext: Option<&str>,
    ) -> (String, Vec<String>) {
        let mut warnings: Vec<String> = Vec::new();
        let out = sql
            .emit_slice_from_nodes(targets, lib_name, ext, |msg| warnings.push(msg))
            .expect("slice emission should succeed");
        (out, warnings)
    }

    fn trigger_entity(function_name: &'static str) -> PgTriggerEntity<'static> {
        PgTriggerEntity {
            function_name,
            to_sql_config: to_sql_config(),
            file: "test.rs",
            line: 1,
            module_path: "tests",
            full_path: Box::leak(format!("tests::{function_name}").into_boxed_str()),
        }
    }

    fn ord_entity(name: &'static str) -> PostgresOrdEntity<'static> {
        // full_path lives under `ord_for::` to avoid colliding with the
        // underlying type's full_path (which is `tests::{name}`). The `name`
        // field is what appears in CREATE OPERATOR FAMILY / CLASS.
        PostgresOrdEntity {
            name,
            file: "test.rs",
            line: 1,
            full_path: Box::leak(format!("tests::ord_for::{name}").into_boxed_str()),
            module_path: "tests::ord_for",
            type_ident: Box::leak(format!("tests::{name}").into_boxed_str()),
            to_sql_config: to_sql_config(),
        }
    }

    fn hash_entity(name: &'static str) -> PostgresHashEntity<'static> {
        // Same disambiguation rationale as `ord_entity`.
        PostgresHashEntity {
            name,
            file: "test.rs",
            line: 1,
            full_path: Box::leak(format!("tests::hash_for::{name}").into_boxed_str()),
            module_path: "tests::hash_for",
            type_ident: Box::leak(format!("tests::{name}").into_boxed_str()),
            to_sql_config: to_sql_config(),
        }
    }

    fn enum_entity(name: &'static str) -> PostgresEnumEntity<'static> {
        PostgresEnumEntity {
            name,
            file: "test.rs",
            line: 1,
            full_path: Box::leak(format!("tests::{name}").into_boxed_str()),
            module_path: "tests",
            type_ident: Box::leak(format!("tests::{name}").into_boxed_str()),
            variants: vec!["red", "green", "blue"],
            to_sql_config: to_sql_config(),
        }
    }

    #[test]
    fn alter_extension_attaches_bare_function() {
        let fun = function_entity("state_fn", vec![], PgExternReturnEntity::None);
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(fun)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, warnings) =
            slice_with_warnings(&sql, &["state_fn".into()], "myext", Some("myext"));
        assert!(warnings.is_empty(), "unexpected warnings: {warnings:?}");
        assert!(out.starts_with("BEGIN;"), "missing BEGIN:\n{out}");
        assert!(out.trim_end().ends_with("COMMIT;"), "missing COMMIT:\n{out}");
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "state_fn"();"#),
            "missing ADD FUNCTION:\n{out}"
        );
    }

    #[test]
    fn alter_extension_includes_argument_types() {
        let arg_ty = external_type("alloc::string::String", "alloc::string::String", "text");
        let fun = function_entity(
            "takes_text",
            vec![PgExternArgumentEntity { pattern: "value", used_ty: arg_ty }],
            PgExternReturnEntity::None,
        );
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(fun)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["takes_text".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "takes_text"(text);"#),
            "missing argtype in ADD FUNCTION:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_operator_in_addition_to_function() {
        let arg_ty = external_type("alloc::string::String", "alloc::string::String", "text");
        let mut fun = function_entity(
            "eq_ignoring_case",
            vec![
                PgExternArgumentEntity { pattern: "lhs", used_ty: arg_ty.clone() },
                PgExternArgumentEntity { pattern: "rhs", used_ty: arg_ty },
            ],
            PgExternReturnEntity::Type { ty: external_type("bool", "bool", "bool") },
        );
        fun.operator = Some(PgOperatorEntity {
            opname: Some("==="),
            commutator: None,
            negator: None,
            restrict: None,
            join: None,
            hashes: false,
            merges: false,
        });

        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(fun)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) =
            slice_with_warnings(&sql, &["eq_ignoring_case".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "eq_ignoring_case"(text, text);"#),
            "missing ADD FUNCTION:\n{out}"
        );
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD OPERATOR ===(text, text);"#),
            "missing ADD OPERATOR:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_type_and_its_io_functions() {
        let ty = type_entity("MyType", "tests::MyType", "tests::MyType");
        let in_fn = function_entity(
            "in_fn",
            vec![PgExternArgumentEntity {
                pattern: "input",
                used_ty: external_type("&core::ffi::CStr", "&core::ffi::CStr", "cstring"),
            }],
            PgExternReturnEntity::Type {
                ty: used_type(
                    "tests::MyType",
                    "tests::MyType",
                    "MyType",
                    TypeOrigin::ThisExtension,
                ),
            },
        );
        let out_fn = function_entity(
            "out_fn",
            vec![PgExternArgumentEntity {
                pattern: "input",
                used_ty: used_type(
                    "tests::MyType",
                    "tests::MyType",
                    "MyType",
                    TypeOrigin::ThisExtension,
                ),
            }],
            PgExternReturnEntity::Type {
                ty: external_type("alloc::ffi::CString", "alloc::ffi::CString", "cstring"),
            },
        );

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Type(ty),
                SqlGraphEntity::Function(in_fn),
                SqlGraphEntity::Function(out_fn),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["tests::MyType".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD TYPE MyType;"#),
            "missing ADD TYPE:\n{out}"
        );
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "in_fn"(cstring);"#),
            "missing ADD FUNCTION for in_fn:\n{out}"
        );
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "out_fn"(MyType);"#),
            "missing ADD FUNCTION for out_fn:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_enum() {
        let en = enum_entity("Color");
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Enum(en)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["Color".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD TYPE Color;"#),
            "missing ADD TYPE:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_aggregate_with_args() {
        let stype = external_type("tests::State", "tests::State", "TEXT");
        let arg_ty = external_type("i32", "i32", "integer");
        let agg = aggregate_entity(
            "sum_my",
            vec![AggregateTypeEntity { used_ty: arg_ty, name: Some("value") }],
            stype,
            None,
        );

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Function(state_function()),
                SqlGraphEntity::Aggregate(agg),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["sum_my".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD AGGREGATE "sum_my"(integer);"#),
            "missing ADD AGGREGATE:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_trigger() {
        let trig = trigger_entity("my_trig");
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Trigger(trig)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["my_trig".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD FUNCTION "my_trig"();"#),
            "missing ADD FUNCTION (trigger):\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_ord_emits_family_and_class() {
        // Build the underlying type + comparison functions so the Ord node
        // has something to connect to. We don't assert on those; we only
        // care that the Ord node's ALTER EXTENSION clauses come out right.
        let mut ty = type_entity("Sortable", "tests::Sortable", "tests::Sortable");
        ty.in_fn_path = "sortable_in";
        ty.out_fn_path = "sortable_out";
        let text = external_type("alloc::string::String", "alloc::string::String", "text");
        let cstring = external_type("&core::ffi::CStr", "&core::ffi::CStr", "cstring");
        let in_fn = function_entity(
            "sortable_in",
            vec![PgExternArgumentEntity { pattern: "input", used_ty: cstring }],
            PgExternReturnEntity::Type {
                ty: used_type(
                    "tests::Sortable",
                    "tests::Sortable",
                    "Sortable",
                    TypeOrigin::ThisExtension,
                ),
            },
        );
        let out_fn = function_entity(
            "sortable_out",
            vec![PgExternArgumentEntity {
                pattern: "input",
                used_ty: used_type(
                    "tests::Sortable",
                    "tests::Sortable",
                    "Sortable",
                    TypeOrigin::ThisExtension,
                ),
            }],
            PgExternReturnEntity::Type { ty: text },
        );
        let ord = ord_entity("Sortable");

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Type(ty),
                SqlGraphEntity::Function(in_fn),
                SqlGraphEntity::Function(out_fn),
                SqlGraphEntity::Ord(ord.clone()),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let ord_idx = sql.ords[&ord];
        let (out, _) = slice_by_nodes(&sql, &[ord_idx], "myext", Some("myext"));
        assert!(
            out.contains(
                r#"ALTER EXTENSION "myext" ADD OPERATOR FAMILY Sortable_btree_ops USING btree;"#
            ),
            "missing ADD OPERATOR FAMILY:\n{out}"
        );
        assert!(
            out.contains(
                r#"ALTER EXTENSION "myext" ADD OPERATOR CLASS Sortable_btree_ops USING btree;"#
            ),
            "missing ADD OPERATOR CLASS:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_hash_emits_family_and_class() {
        let mut ty = type_entity("Hashable", "tests::Hashable", "tests::Hashable");
        ty.in_fn_path = "hashable_in";
        ty.out_fn_path = "hashable_out";
        let text = external_type("alloc::string::String", "alloc::string::String", "text");
        let cstring = external_type("&core::ffi::CStr", "&core::ffi::CStr", "cstring");
        let in_fn = function_entity(
            "hashable_in",
            vec![PgExternArgumentEntity { pattern: "input", used_ty: cstring }],
            PgExternReturnEntity::Type {
                ty: used_type(
                    "tests::Hashable",
                    "tests::Hashable",
                    "Hashable",
                    TypeOrigin::ThisExtension,
                ),
            },
        );
        let out_fn = function_entity(
            "hashable_out",
            vec![PgExternArgumentEntity {
                pattern: "input",
                used_ty: used_type(
                    "tests::Hashable",
                    "tests::Hashable",
                    "Hashable",
                    TypeOrigin::ThisExtension,
                ),
            }],
            PgExternReturnEntity::Type { ty: text },
        );
        let hash = hash_entity("Hashable");

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Type(ty),
                SqlGraphEntity::Function(in_fn),
                SqlGraphEntity::Function(out_fn),
                SqlGraphEntity::Hash(hash.clone()),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let hash_idx = sql.hashes[&hash];
        let (out, _) = slice_by_nodes(&sql, &[hash_idx], "myext", Some("myext"));
        assert!(
            out.contains(
                r#"ALTER EXTENSION "myext" ADD OPERATOR FAMILY Hashable_hash_ops USING hash;"#
            ),
            "missing ADD OPERATOR FAMILY:\n{out}"
        );
        assert!(
            out.contains(
                r#"ALTER EXTENSION "myext" ADD OPERATOR CLASS Hashable_hash_ops USING hash;"#
            ),
            "missing ADD OPERATOR CLASS:\n{out}"
        );
    }

    #[test]
    fn alter_extension_attaches_schema_but_skips_public() {
        let schema = schema_entity("tests::my_schema", "my_schema");
        let fun_arg = external_type("i32", "i32", "integer");
        let mut fun = function_entity(
            "my_fn",
            vec![PgExternArgumentEntity { pattern: "x", used_ty: fun_arg }],
            PgExternReturnEntity::None,
        );
        fun.module_path = "tests::my_schema";
        fun.full_path = "tests::my_schema::my_fn";

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::Schema(schema),
                SqlGraphEntity::Function(fun),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) =
            slice_with_warnings(&sql, &["tests::my_schema::my_fn".into()], "myext", Some("myext"));
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD SCHEMA my_schema;"#),
            "missing ADD SCHEMA:\n{out}"
        );
        assert!(
            !out.contains("ADD SCHEMA public"),
            "should not emit ADD SCHEMA for public:\n{out}"
        );
    }

    #[test]
    fn alter_extension_custom_sql_with_creates_emits_add_type() {
        let hexint = extension_owned_type("tests::HexInt", "tests::HexInt", "hexint");
        let declared = declared_type_sql(
            "tests",
            "tests::concrete_type",
            "concrete_type",
            "tests::HexInt",
            "tests::HexInt",
            "hexint",
        );
        let target =
            function_entity("uses_hexint", vec![], PgExternReturnEntity::Type { ty: hexint });

        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::CustomSql(declared),
                SqlGraphEntity::Function(target),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, warnings) =
            slice_with_warnings(&sql, &["uses_hexint".into()], "myext", Some("myext"));
        assert!(warnings.is_empty(), "unexpected warnings: {warnings:?}");
        assert!(
            out.contains(r#"ALTER EXTENSION "myext" ADD TYPE hexint;"#),
            "missing ADD TYPE for declared type:\n{out}"
        );
    }

    #[test]
    fn alter_extension_custom_sql_without_creates_warns() {
        let free_form = ExtensionSqlEntity {
            module_path: "tests",
            full_path: "tests::free_form_sql",
            sql: "CREATE TABLE some_table(id INT);",
            file: "somefile.rs",
            line: 42,
            name: "free_form_sql",
            bootstrap: false,
            finalize: false,
            requires: vec![],
            creates: vec![],
        };

        // Emit a function that transitively pulls the free-form block in
        // through a `requires`. Simplest path: slice the free-form block
        // directly by name.
        let sql = PgrxSql::build(
            vec![
                SqlGraphEntity::ExtensionRoot(control_file()),
                SqlGraphEntity::CustomSql(free_form),
            ]
            .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, warnings) =
            slice_with_warnings(&sql, &["free_form_sql".into()], "myext", Some("myext"));
        assert!(
            !out.contains(r#"ALTER EXTENSION "myext" ADD"#),
            "free-form block should not emit ADD:\n{out}"
        );
        assert_eq!(warnings.len(), 1, "expected one warning, got: {warnings:?}");
        assert!(warnings[0].contains("somefile.rs:42"), "warning missing file:line: {warnings:?}");
        assert!(
            warnings[0].contains("free-form") || warnings[0].contains("creates"),
            "warning missing reason: {warnings:?}"
        );
    }

    #[test]
    fn no_alter_extension_mode_matches_pre_feature_output() {
        let fun = function_entity("state_fn", vec![], PgExternReturnEntity::None);
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(fun)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["state_fn".into()], "myext", None);
        assert!(!out.contains("ALTER EXTENSION"), "unexpected ALTER EXTENSION:\n{out}");
        assert!(!out.contains("BEGIN;"), "unexpected BEGIN:\n{out}");
        assert!(!out.contains("COMMIT;"), "unexpected COMMIT:\n{out}");
    }

    #[test]
    fn alter_extension_substitutes_module_pathname() {
        let fun = function_entity("state_fn", vec![], PgExternReturnEntity::None);
        let sql = PgrxSql::build(
            vec![SqlGraphEntity::ExtensionRoot(control_file()), SqlGraphEntity::Function(fun)]
                .into_iter(),
            "myext".into(),
            false,
        )
        .unwrap();

        let (out, _) = slice_with_warnings(&sql, &["state_fn".into()], "myext", Some("myext"));
        assert!(out.contains("'$libdir/myext'"), "missing libdir substitution:\n{out}");
        assert!(!out.contains("'MODULE_PATHNAME'"), "raw placeholder leaked:\n{out}");
    }
}
