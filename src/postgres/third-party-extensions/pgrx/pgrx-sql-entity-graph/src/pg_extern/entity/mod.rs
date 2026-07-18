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

`#[pg_extern]` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
mod argument;
mod cast;
mod operator;
mod returning;

pub use argument::PgExternArgumentEntity;
pub use cast::PgCastEntity;
pub use operator::PgOperatorEntity;
pub use returning::{PgExternReturnEntity, PgExternReturnEntityIteratedItem};

use crate::fmt;
use crate::metadata::{Returns, SqlMapping};
use crate::pgrx_sql::PgrxSql;
use crate::to_sql::entity::ToSqlConfigEntity;
use crate::to_sql::ToSql;
use crate::{ExternArgs, SqlGraphEntity, SqlGraphIdentifier, TypeMatch};

use eyre::{eyre, WrapErr};

/// The output of a [`PgExtern`](crate::pg_extern::PgExtern) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct PgExternEntity {
    pub name: &'static str,
    pub unaliased_name: &'static str,
    pub module_path: &'static str,
    pub full_path: &'static str,
    pub metadata: crate::metadata::FunctionMetadataEntity,
    pub fn_args: Vec<PgExternArgumentEntity>,
    pub fn_return: PgExternReturnEntity,
    pub schema: Option<&'static str>,
    pub file: &'static str,
    pub line: u32,
    pub extern_attrs: Vec<ExternArgs>,
    pub search_path: Option<Vec<&'static str>>,
    pub operator: Option<PgOperatorEntity>,
    pub cast: Option<PgCastEntity>,
    pub to_sql_config: ToSqlConfigEntity,
}

impl From<PgExternEntity> for SqlGraphEntity {
    fn from(val: PgExternEntity) -> Self {
        SqlGraphEntity::Function(val)
    }
}

impl SqlGraphIdentifier for PgExternEntity {
    fn dot_identifier(&self) -> String {
        format!("fn {}", self.name)
    }
    fn rust_identifier(&self) -> String {
        self.full_path.to_string()
    }

    fn file(&self) -> Option<&'static str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl ToSql for PgExternEntity {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String> {
        let self_index = context.externs[self];
        let mut extern_attrs = self.extern_attrs.clone();
        // if we already have a STRICT marker we do not need to add it
        // presume we can upgrade, then disprove it
        let mut strict_upgrade = !extern_attrs.iter().any(|i| i == &ExternArgs::Strict);
        if strict_upgrade {
            // It may be possible to infer a `STRICT` marker though.
            // But we can only do that if the user hasn't used `Option<T>` or `pgrx::Internal`
            for arg in &self.metadata.arguments {
                if arg.optional {
                    strict_upgrade = false;
                }
            }
        }

        if strict_upgrade {
            extern_attrs.push(ExternArgs::Strict);
        }
        extern_attrs.sort();
        extern_attrs.dedup();

        let module_pathname = &context.get_module_pathname();
        let schema = self
            .schema
            .map(|schema| format!("{schema}."))
            .unwrap_or_else(|| context.schema_prefix_for(&self_index));
        let arguments = if !self.fn_args.is_empty() {
            let mut args = Vec::new();
            let metadata_without_arg_skips = &self
                .metadata
                .arguments
                .iter()
                .filter(|v| v.argument_sql != Ok(SqlMapping::Skip))
                .collect::<Vec<_>>();
            for (idx, arg) in self.fn_args.iter().enumerate() {
                let graph_index = context
                    .graph
                    .neighbors_undirected(self_index)
                    .find(|neighbor| context.graph[*neighbor].type_matches(&arg.used_ty))
                    .ok_or_else(|| eyre!("Could not find arg type in graph. Got: {:?}", arg))?;
                let needs_comma = idx < (metadata_without_arg_skips.len().saturating_sub(1));
                let metadata_argument = &self.metadata.arguments[idx];
                match metadata_argument.argument_sql {
                    Ok(SqlMapping::As(ref argument_sql)) => {
                        let buf = format!("\
                                            \t\"{pattern}\" {variadic}{schema_prefix}{sql_type}{default}{maybe_comma}/* {type_name} */\
                                        ",
                                            pattern = arg.pattern,
                                            schema_prefix = context.schema_prefix_for(&graph_index),
                                            // First try to match on [`TypeId`] since it's most reliable.
                                            sql_type = argument_sql,
                                            default = if let Some(def) = arg.used_ty.default { format!(" DEFAULT {def}") } else { String::from("") },
                                            variadic = if metadata_argument.variadic { "VARIADIC " } else { "" },
                                            maybe_comma = if needs_comma { ", " } else { " " },
                                            type_name = metadata_argument.type_name,
                                    );
                        args.push(buf);
                    }
                    Ok(SqlMapping::Composite { array_brackets }) => {
                        let sql = self.fn_args[idx]
                            .used_ty
                            .composite_type
                            .map(|v| fmt::with_array_brackets(v.into(), array_brackets))
                            .ok_or_else(|| {
                                eyre!(
                                    "Macro expansion time suggested a composite_type!() in return"
                                )
                            })?;
                        let buf = format!("\
                            \t\"{pattern}\" {variadic}{schema_prefix}{sql_type}{default}{maybe_comma}/* {type_name} */\
                        ",
                            pattern = arg.pattern,
                            schema_prefix = context.schema_prefix_for(&graph_index),
                            // First try to match on [`TypeId`] since it's most reliable.
                            sql_type = sql,
                            default = if let Some(def) = arg.used_ty.default { format!(" DEFAULT {def}") } else { String::from("") },
                            variadic = if metadata_argument.variadic { "VARIADIC " } else { "" },
                            maybe_comma = if needs_comma { ", " } else { " " },
                            type_name = metadata_argument.type_name,
                    );
                        args.push(buf);
                    }
                    Ok(SqlMapping::Skip) => (),
                    Err(err) => return Err(err).wrap_err("While mapping argument"),
                }
            }
            String::from("\n") + &args.join("\n") + "\n"
        } else {
            Default::default()
        };

        let returns = match &self.fn_return {
            PgExternReturnEntity::None => String::from("RETURNS void"),
            PgExternReturnEntity::Type { ty } => {
                let graph_index = context
                    .graph
                    .neighbors_undirected(self_index)
                    .find(|neighbor| context.graph[*neighbor].type_matches(ty))
                    .ok_or_else(|| eyre!("Could not find return type in graph."))?;
                let metadata_retval = self.metadata.retval.clone();
                let sql_type = match metadata_retval.return_sql {
                    Ok(Returns::One(SqlMapping::As(ref sql))) => sql.clone(),
                    Ok(Returns::One(SqlMapping::Composite { array_brackets })) => fmt::with_array_brackets(ty.composite_type.unwrap().into(), array_brackets),
                    Ok(other) => return Err(eyre!("Got non-plain mapped/composite return variant SQL in what macro-expansion thought was a type, got: {other:?}")),
                    Err(err) => return Err(err).wrap_err("Error mapping return SQL")
                };
                format!(
                    "RETURNS {schema_prefix}{sql_type} /* {full_path} */",
                    schema_prefix = context.schema_prefix_for(&graph_index),
                    full_path = ty.full_path
                )
            }
            PgExternReturnEntity::SetOf { ty, .. } => {
                let graph_index = context
                    .graph
                    .neighbors_undirected(self_index)
                    .find(|neighbor| context.graph[*neighbor].type_matches(ty))
                    .ok_or_else(|| eyre!("Could not find return type in graph."))?;
                let metadata_retval = self.metadata.retval.clone();
                let sql_type = match metadata_retval.return_sql {
                        Ok(Returns::SetOf(SqlMapping::As(ref sql))) => sql.clone(),
                        Ok(Returns::SetOf(SqlMapping::Composite { array_brackets })) => fmt::with_array_brackets(ty.composite_type.unwrap().into(), array_brackets),
                        Ok(_other) => return Err(eyre!("Got non-setof mapped/composite return variant SQL in what macro-expansion thought was a setof")),
                        Err(err) => return Err(err).wrap_err("Error mapping return SQL"),
                    };
                format!(
                    "RETURNS SETOF {schema_prefix}{sql_type} /* {full_path} */",
                    schema_prefix = context.schema_prefix_for(&graph_index),
                    full_path = ty.full_path
                )
            }
            PgExternReturnEntity::Iterated { tys: table_items, .. } => {
                let mut items = String::new();
                let metadata_retval = self.metadata.retval.clone();
                let metadata_retval_sqls: Vec<String> = match metadata_retval.return_sql {
                        Ok(Returns::Table(variants)) => {
                            variants.iter().enumerate().map(|(idx, variant)| {
                                match variant {
                                    SqlMapping::As(sql) => sql.clone(),
                                    SqlMapping::Composite { array_brackets } => {
                                        let composite = table_items[idx].ty.composite_type.unwrap();
                                        fmt::with_array_brackets(composite.into(), *array_brackets)
                                    },
                                    SqlMapping::Skip => todo!(),
                                }
                            }).collect()
                        },
                        Ok(_other) => return Err(eyre!("Got non-table return variant SQL in what macro-expansion thought was a table")),
                        Err(err) => return Err(err).wrap_err("Error mapping return SQL"),
                    };

                for (idx, returning::PgExternReturnEntityIteratedItem { ty, name: col_name }) in
                    table_items.iter().enumerate()
                {
                    let graph_index =
                        context.graph.neighbors_undirected(self_index).find(|neighbor| {
                            context.graph[*neighbor].id_or_name_matches(&ty.ty_id, ty.ty_source)
                        });

                    let needs_comma = idx < (table_items.len() - 1);
                    let item = format!(
                        "\n\t{col_name} {schema_prefix}{ty_resolved}{needs_comma} /* {ty_name} */",
                        col_name = col_name.expect(
                            "An iterator of tuples should have `named!()` macro declarations."
                        ),
                        schema_prefix = if let Some(graph_index) = graph_index {
                            context.schema_prefix_for(&graph_index)
                        } else {
                            "".into()
                        },
                        ty_resolved = metadata_retval_sqls[idx],
                        needs_comma = if needs_comma { ", " } else { " " },
                        ty_name = ty.full_path
                    );
                    items.push_str(&item);
                }
                format!("RETURNS TABLE ({items}\n)")
            }
            PgExternReturnEntity::Trigger => String::from("RETURNS trigger"),
        };
        let PgExternEntity { name, module_path, file, line, .. } = self;

        let fn_sql = format!(
            "\
                CREATE {or_replace} FUNCTION {schema}\"{name}\"({arguments}) {returns}\n\
                {extern_attrs}\
                {search_path}\
                LANGUAGE c /* Rust */\n\
                AS '{module_pathname}', '{unaliased_name}_wrapper';\
            ",
            or_replace =
                if extern_attrs.contains(&ExternArgs::CreateOrReplace) { "OR REPLACE" } else { "" },
            search_path = if let Some(search_path) = &self.search_path {
                let retval = format!("SET search_path TO {}", search_path.join(", "));
                retval + "\n"
            } else {
                Default::default()
            },
            extern_attrs = if extern_attrs.is_empty() {
                String::default()
            } else {
                let mut retval = extern_attrs
                    .iter()
                    .filter(|attr| **attr != ExternArgs::CreateOrReplace)
                    .map(|attr| attr.to_string().to_uppercase())
                    .collect::<Vec<_>>()
                    .join(" ");
                retval.push('\n');
                retval
            },
            unaliased_name = self.unaliased_name,
        );

        let requires = {
            let requires_attrs = self
                .extern_attrs
                .iter()
                .filter_map(|x| match x {
                    ExternArgs::Requires(requirements) => Some(requirements),
                    _ => None,
                })
                .flatten()
                .collect::<Vec<_>>();
            if !requires_attrs.is_empty() {
                format!(
                    "-- requires:\n{}\n",
                    requires_attrs
                        .iter()
                        .map(|i| format!("--   {i}"))
                        .collect::<Vec<_>>()
                        .join("\n")
                )
            } else {
                "".to_string()
            }
        };

        let mut ext_sql = format!(
            "\n\
            -- {file}:{line}\n\
            -- {module_path}::{name}\n\
            {requires}\
            {fn_sql}"
        );

        if let Some(op) = &self.operator {
            let mut optionals = vec![];
            if let Some(it) = op.commutator {
                optionals.push(format!("\tCOMMUTATOR = {it}"));
            };
            if let Some(it) = op.negator {
                optionals.push(format!("\tNEGATOR = {it}"));
            };
            if let Some(it) = op.restrict {
                optionals.push(format!("\tRESTRICT = {it}"));
            };
            if let Some(it) = op.join {
                optionals.push(format!("\tJOIN = {it}"));
            };
            if op.hashes {
                optionals.push(String::from("\tHASHES"));
            };
            if op.merges {
                optionals.push(String::from("\tMERGES"));
            };

            let left_arg =
                self.metadata.arguments.first().ok_or_else(|| {
                    eyre!("Did not find `left_arg` for operator `{}`.", self.name)
                })?;
            let left_fn_arg = self
                .fn_args
                .first()
                .ok_or_else(|| eyre!("Did not find `left_arg` for operator `{}`.", self.name))?;
            let left_arg_graph_index = context
                .graph
                .neighbors_undirected(self_index)
                .find(|neighbor| {
                    context.graph[*neighbor]
                        .id_or_name_matches(&left_fn_arg.used_ty.ty_id, left_arg.type_name)
                })
                .ok_or_else(|| {
                    eyre!("Could not find left arg type in graph. Got: {:?}", left_arg)
                })?;
            let left_arg_sql = match left_arg.argument_sql {
                Ok(SqlMapping::As(ref sql)) => sql.clone(),
                Ok(SqlMapping::Composite { array_brackets }) => {
                    if array_brackets {
                        let composite_type = self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?;
                        format!("{composite_type}[]")
                    } else {
                        self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?.to_string()
                    }
                }
                Ok(SqlMapping::Skip) => {
                    return Err(eyre!(
                        "Found an skipped SQL type in an operator, this is not valid"
                    ))
                }
                Err(err) => return Err(err.into()),
            };

            let right_arg =
                self.metadata.arguments.get(1).ok_or_else(|| {
                    eyre!("Did not find `left_arg` for operator `{}`.", self.name)
                })?;
            let right_fn_arg = self
                .fn_args
                .get(1)
                .ok_or_else(|| eyre!("Did not find `left_arg` for operator `{}`.", self.name))?;
            let right_arg_graph_index = context
                .graph
                .neighbors_undirected(self_index)
                .find(|neighbor| {
                    context.graph[*neighbor]
                        .id_or_name_matches(&right_fn_arg.used_ty.ty_id, right_arg.type_name)
                })
                .ok_or_else(|| {
                    eyre!("Could not find right arg type in graph. Got: {:?}", right_arg)
                })?;
            let right_arg_sql = match right_arg.argument_sql {
                Ok(SqlMapping::As(ref sql)) => sql.clone(),
                Ok(SqlMapping::Composite { array_brackets }) => {
                    if array_brackets {
                        let composite_type = self.fn_args[1].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?;
                        format!("{composite_type}[]")
                    } else {
                        self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?.to_string()
                    }
                }
                Ok(SqlMapping::Skip) => {
                    return Err(eyre!(
                        "Found an skipped SQL type in an operator, this is not valid"
                    ))
                }
                Err(err) => return Err(err.into()),
            };

            let schema = self
                .schema
                .map(|schema| format!("{schema}."))
                .unwrap_or_else(|| context.schema_prefix_for(&self_index));

            let operator_sql = format!("\n\n\
                                                    -- {file}:{line}\n\
                                                    -- {module_path}::{name}\n\
                                                    CREATE OPERATOR {schema}{opname} (\n\
                                                        \tPROCEDURE={schema}\"{name}\",\n\
                                                        \tLEFTARG={schema_prefix_left}{left_arg_sql}, /* {left_name} */\n\
                                                        \tRIGHTARG={schema_prefix_right}{right_arg_sql}{maybe_comma} /* {right_name} */\n\
                                                        {optionals}\
                                                    );\
                                                    ",
                                                    opname = op.opname.unwrap(),
                                                    left_name = left_arg.type_name,
                                                    right_name = right_arg.type_name,
                                                    schema_prefix_left = context.schema_prefix_for(&left_arg_graph_index),
                                                    schema_prefix_right = context.schema_prefix_for(&right_arg_graph_index),
                                                    maybe_comma = if !optionals.is_empty() { "," } else { "" },
                                                    optionals = if !optionals.is_empty() { optionals.join(",\n") + "\n" } else { "".to_string() },
                                            );
            ext_sql += &operator_sql
        };
        if let Some(cast) = &self.cast {
            let target_arg = &self.metadata.retval;
            let target_fn_arg = &self.fn_return;
            let target_arg_graph_index = context
                .graph
                .neighbors_undirected(self_index)
                .find(|neighbor| match (&context.graph[*neighbor], target_fn_arg) {
                    (SqlGraphEntity::Type(ty), PgExternReturnEntity::Type { ty: rty }) => {
                        ty.id_matches(&rty.ty_id)
                    }
                    (SqlGraphEntity::Enum(en), PgExternReturnEntity::Type { ty: rty }) => {
                        en.id_matches(&rty.ty_id)
                    }
                    (SqlGraphEntity::BuiltinType(defined), _) => defined == target_arg.type_name,
                    _ => false,
                })
                .ok_or_else(|| {
                    eyre!("Could not find source type in graph. Got: {:?}", target_arg)
                })?;
            let target_arg_sql = match target_arg.argument_sql {
                Ok(SqlMapping::As(ref sql)) => sql.clone(),
                Ok(SqlMapping::Composite { array_brackets }) => {
                    if array_brackets {
                        let composite_type = self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?;
                        format!("{composite_type}[]")
                    } else {
                        self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?.to_string()
                    }
                }
                Ok(SqlMapping::Skip) => {
                    return Err(eyre!("Found an skipped SQL type in a cast, this is not valid"))
                }
                Err(err) => return Err(err.into()),
            };
            if self.metadata.arguments.len() != 1 {
                return Err(eyre!(
                    "PG cast function ({}) must have exactly one argument, got {}",
                    self.name,
                    self.metadata.arguments.len()
                ));
            }
            if self.fn_args.len() != 1 {
                return Err(eyre!(
                    "PG cast function ({}) must have exactly one argument, got {}",
                    self.name,
                    self.fn_args.len()
                ));
            }
            let source_arg = self
                .metadata
                .arguments
                .first()
                .ok_or_else(|| eyre!("Did not find source type for cast `{}`.", self.name))?;
            let source_fn_arg = self
                .fn_args
                .first()
                .ok_or_else(|| eyre!("Did not find source type for cast `{}`.", self.name))?;
            let source_arg_graph_index = context
                .graph
                .neighbors_undirected(self_index)
                .find(|neighbor| {
                    context.graph[*neighbor]
                        .id_or_name_matches(&source_fn_arg.used_ty.ty_id, source_arg.type_name)
                })
                .ok_or_else(|| {
                    eyre!("Could not find source type in graph. Got: {:?}", source_arg)
                })?;
            let source_arg_sql = match source_arg.argument_sql {
                Ok(SqlMapping::As(ref sql)) => sql.clone(),
                Ok(SqlMapping::Composite { array_brackets }) => {
                    if array_brackets {
                        let composite_type = self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?;
                        format!("{composite_type}[]")
                    } else {
                        self.fn_args[0].used_ty.composite_type
                            .ok_or(eyre!("Found a composite type but macro expansion time did not reveal a name, use `pgrx::composite_type!()`"))?.to_string()
                    }
                }
                Ok(SqlMapping::Skip) => {
                    return Err(eyre!("Found an skipped SQL type in a cast, this is not valid"))
                }
                Err(err) => return Err(err.into()),
            };
            let optional = match cast {
                PgCastEntity::Default => String::from(""),
                PgCastEntity::Assignment => String::from(" AS ASSIGNMENT"),
                PgCastEntity::Implicit => String::from(" AS IMPLICIT"),
            };

            let cast_sql = format!("\n\n\
                                                    -- {file}:{line}\n\
                                                    -- {module_path}::{name}\n\
                                                    CREATE CAST (\n\
                                                        \t{schema_prefix_source}{source_arg_sql} /* {source_name} */\n\
                                                        \tAS\n\
                                                        \t{schema_prefix_target}{target_arg_sql} /* {target_name} */\n\
                                                    )\n\
                                                    WITH FUNCTION {function_name}{optional};\
                                                    ",
                                                    file = self.file,
                                                    line = self.line,
                                                    name = self.name,
                                                    module_path = self.module_path,
                                                    schema_prefix_source = context.schema_prefix_for(&source_arg_graph_index),
                                                    source_name = source_arg.type_name,
                                                    schema_prefix_target = context.schema_prefix_for(&target_arg_graph_index),
                                                    target_name = target_arg.type_name,
                                                    function_name = self.name,
                                            );
            ext_sql += &cast_sql
        };
        Ok(ext_sql)
    }
}
