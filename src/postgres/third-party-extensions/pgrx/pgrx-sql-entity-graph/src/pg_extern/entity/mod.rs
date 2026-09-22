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

use crate::UsedTypeEntity;
use crate::fmt;
use crate::metadata::{Returns, SqlArrayMapping, SqlMapping};
use crate::pgrx_sql::PgrxSql;
use crate::to_sql::ToSql;
use crate::to_sql::entity::ToSqlConfigEntity;
use crate::{ExternArgs, SqlGraphEntity, SqlGraphIdentifier};

use eyre::{WrapErr, eyre};
use petgraph::graph::NodeIndex;

/// The output of a [`PgExtern`](crate::pg_extern::PgExtern) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct PgExternEntity<'a> {
    pub name: &'a str,
    pub unaliased_name: &'a str,
    pub module_path: &'a str,
    pub full_path: &'a str,
    pub fn_args: Vec<PgExternArgumentEntity<'a>>,
    pub fn_return: PgExternReturnEntity<'a>,
    pub schema: Option<&'a str>,
    pub file: &'a str,
    pub line: u32,
    pub extern_attrs: Vec<ExternArgs>,
    pub search_path: Option<Vec<&'a str>>,
    pub operator: Option<PgOperatorEntity<'a>>,
    pub cast: Option<PgCastEntity>,
    pub to_sql_config: ToSqlConfigEntity<'a>,
}

impl<'a> From<PgExternEntity<'a>> for SqlGraphEntity<'a> {
    fn from(val: PgExternEntity<'a>) -> Self {
        SqlGraphEntity::Function(val)
    }
}

impl SqlGraphIdentifier for PgExternEntity<'_> {
    fn dot_identifier(&self) -> String {
        format!("fn {}", self.name)
    }
    fn rust_identifier(&self) -> String {
        self.full_path.to_string()
    }

    fn file(&self) -> Option<&str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl PgExternEntity<'_> {
    fn sql_name(&self, context: &PgrxSql) -> String {
        let self_index = context.externs[self];
        let schema = self
            .schema
            .map(|schema| format!("{schema}."))
            .unwrap_or_else(|| context.schema_prefix_for(&self_index));

        format!("{schema}\"{}\"", self.name)
    }
}

fn composite_sql_type(composite_type: Option<&str>) -> eyre::Result<String> {
    composite_type
        .map(ToString::to_string)
        .ok_or_else(|| eyre!("Composite mapping requires composite_type"))
}

fn array_sql_type(mapping: &SqlArrayMapping, composite_type: Option<&str>) -> eyre::Result<String> {
    Ok(match mapping {
        SqlArrayMapping::As(sql) => fmt::with_array_brackets(sql.clone(), 1),
        SqlArrayMapping::Composite => {
            fmt::with_array_brackets(composite_sql_type(composite_type)?, 1)
        }
    })
}

fn sql_type(mapping: &SqlMapping, composite_type: Option<&str>) -> eyre::Result<String> {
    match mapping {
        SqlMapping::As(sql) => Ok(sql.clone()),
        SqlMapping::Composite => composite_sql_type(composite_type),
        SqlMapping::Array(value) => array_sql_type(value, composite_type),
        SqlMapping::Skip => Err(eyre!("Found a skipped SQL type where SQL should be emitted")),
    }
}

/// Render the SQL spelling of one `UsedType`, with schema prefix applied.
/// This is the bit that sits right after `"name" VARIADIC ` in a CREATE
/// FUNCTION signature.
pub(crate) fn render_used_type_sql(
    context: &PgrxSql,
    owner: NodeIndex,
    slot: &str,
    used_ty: &UsedTypeEntity,
) -> eyre::Result<String> {
    let schema_prefix = context.schema_prefix_for_used_type(&owner, slot, used_ty)?;
    let body = match used_ty.metadata.argument_sql {
        Ok(SqlMapping::As(ref sql)) => sql.clone(),
        Ok(ref mapping @ (SqlMapping::Composite | SqlMapping::Array(_))) => {
            sql_type(mapping, used_ty.composite_type)?
        }
        Ok(SqlMapping::Skip) => {
            return Err(eyre!("Found a skipped SQL type where SQL should be emitted"));
        }
        Err(err) => return Err(err.into()),
    };
    Ok(format!("{schema_prefix}{body}"))
}

/// Render the comma-separated argument-type list for a pg_extern, matching
/// the positional shape of CREATE FUNCTION but without names or defaults.
/// Skipped args are filtered out; variadic args get a `VARIADIC ` prefix.
pub(crate) fn render_function_argtypes(
    context: &PgrxSql,
    owner: NodeIndex,
    f: &PgExternEntity,
) -> eyre::Result<String> {
    let mut pieces = Vec::new();
    for arg in f.fn_args.iter().filter(|a| a.used_ty.emits_argument_sql()) {
        let slot = format!("argument `{}`", arg.pattern);
        let rendered = render_used_type_sql(context, owner, &slot, &arg.used_ty)?;
        if arg.used_ty.variadic {
            pieces.push(format!("VARIADIC {rendered}"));
        } else {
            pieces.push(rendered);
        }
    }
    Ok(pieces.join(", "))
}

/// Render the return type of a pg_extern for contexts that need a plain
/// scalar, such as CREATE CAST. Errors if the function returns a set or a
/// table.
pub(crate) fn render_function_return_type(
    context: &PgrxSql,
    owner: NodeIndex,
    f: &PgExternEntity,
) -> eyre::Result<String> {
    let ty = match &f.fn_return {
        PgExternReturnEntity::Type { ty } => ty,
        PgExternReturnEntity::None => {
            return Err(eyre!("Cannot render return type for a function with no return"));
        }
        other => {
            return Err(eyre!("Cannot render a scalar return type for {other:?}"));
        }
    };
    let schema_prefix = context.schema_prefix_for_used_type(&owner, "return type", ty)?;
    let body = match &ty.metadata.return_sql {
        Ok(Returns::One(SqlMapping::As(sql))) => sql.clone(),
        Ok(Returns::One(mapping @ (SqlMapping::Composite | SqlMapping::Array(_)))) => {
            sql_type(mapping, ty.composite_type)?
        }
        Ok(Returns::One(SqlMapping::Skip)) => {
            return Err(eyre!("Return type was SqlMapping::Skip"));
        }
        Ok(other) => {
            return Err(eyre!("Return type is not a scalar: {other:?}"));
        }
        Err(err) => return Err((*err).into()),
    };
    Ok(format!("{schema_prefix}{body}"))
}

impl ToSql for PgExternEntity<'_> {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String> {
        let self_index = context.externs[self];
        let mut extern_attrs = self.extern_attrs.clone();
        // if we already have a STRICT marker we do not need to add it
        // presume we can upgrade, then disprove it
        let mut strict_upgrade = !extern_attrs.iter().any(|i| i == &ExternArgs::Strict);
        if strict_upgrade {
            // It may be possible to infer a `STRICT` marker though.
            // But we can only do that if the user hasn't used a nullable argument wrapper.
            for arg in &self.fn_args {
                if arg.used_ty.optional {
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
            let sql_args = self
                .fn_args
                .iter()
                .filter(|arg| arg.used_ty.emits_argument_sql())
                .collect::<Vec<_>>();
            for (idx, arg) in sql_args.iter().enumerate() {
                let needs_comma = idx < (sql_args.len().saturating_sub(1));
                let schema_prefix = context.schema_prefix_for_used_type(
                    &self_index,
                    &format!("argument `{}`", arg.pattern),
                    &arg.used_ty,
                )?;
                match arg.used_ty.metadata.argument_sql {
                    Ok(SqlMapping::As(ref argument_sql)) => {
                        let buf = format!(
                            "\
                                            \t\"{pattern}\" {variadic}{schema_prefix}{sql_type}{default}{maybe_comma}/* {type_name} */\
                                        ",
                            pattern = arg.pattern,
                            schema_prefix = schema_prefix,
                            // The SQL spelling comes from the embedded schema metadata.
                            sql_type = argument_sql,
                            default = if let Some(def) = arg.used_ty.default {
                                format!(" DEFAULT {def}")
                            } else {
                                String::from("")
                            },
                            variadic = if arg.used_ty.variadic { "VARIADIC " } else { "" },
                            maybe_comma = if needs_comma { ", " } else { " " },
                            type_name = arg.used_ty.full_path,
                        );
                        args.push(buf);
                    }
                    Ok(ref mapping @ (SqlMapping::Composite | SqlMapping::Array(_))) => {
                        let sql = sql_type(mapping, arg.used_ty.composite_type)?;
                        let buf = format!(
                            "\
                            \t\"{pattern}\" {variadic}{schema_prefix}{sql_type}{default}{maybe_comma}/* {type_name} */\
                        ",
                            pattern = arg.pattern,
                            schema_prefix = schema_prefix,
                            // The SQL spelling comes from the embedded schema metadata.
                            sql_type = sql,
                            default = if let Some(def) = arg.used_ty.default {
                                format!(" DEFAULT {def}")
                            } else {
                                String::from("")
                            },
                            variadic = if arg.used_ty.variadic { "VARIADIC " } else { "" },
                            maybe_comma = if needs_comma { ", " } else { " " },
                            type_name = arg.used_ty.full_path,
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
                let (schema_prefix, sql_type) = match &ty.metadata.return_sql {
                    Ok(Returns::One(SqlMapping::As(sql))) => (
                        context.schema_prefix_for_used_type(&self_index, "return type", ty)?,
                        sql.clone(),
                    ),
                    Ok(Returns::One(mapping @ (SqlMapping::Composite | SqlMapping::Array(_)))) => (
                        context.schema_prefix_for_used_type(&self_index, "return type", ty)?,
                        sql_type(mapping, ty.composite_type)?,
                    ),
                    Ok(other) => {
                        return Err(eyre!(
                            "Got non-plain mapped/composite return variant SQL in what macro-expansion thought was a type, got: {other:?}"
                        ));
                    }
                    Err(err) => return Err(*err).wrap_err("Error mapping return SQL"),
                };
                format!(
                    "RETURNS {schema_prefix}{sql_type} /* {full_path} */",
                    full_path = ty.full_path
                )
            }
            PgExternReturnEntity::SetOf { ty, .. } => {
                let (schema_prefix, sql_type) = match &ty.metadata.return_sql {
                    Ok(Returns::One(SqlMapping::As(sql)))
                    | Ok(Returns::SetOf(SqlMapping::As(sql))) => (
                        context.schema_prefix_for_used_type(
                            &self_index,
                            "setof return type",
                            ty,
                        )?,
                        sql.clone(),
                    ),
                    Ok(Returns::One(mapping @ (SqlMapping::Composite | SqlMapping::Array(_))))
                    | Ok(Returns::SetOf(
                        mapping @ (SqlMapping::Composite | SqlMapping::Array(_)),
                    )) => (
                        context.schema_prefix_for_used_type(
                            &self_index,
                            "setof return type",
                            ty,
                        )?,
                        sql_type(mapping, ty.composite_type)?,
                    ),
                    Ok(other) => {
                        return Err(eyre!(
                            "Got non-scalar mapped/composite return variant SQL in what macro-expansion thought was a setof item, got: {other:?}"
                        ));
                    }
                    Err(err) => return Err(*err).wrap_err("Error mapping return SQL"),
                };
                format!(
                    "RETURNS SETOF {schema_prefix}{sql_type} /* {full_path} */",
                    full_path = ty.full_path
                )
            }
            PgExternReturnEntity::Iterated { tys: table_items, .. } => {
                let mut items = String::new();
                for (idx, PgExternReturnEntityIteratedItem { ty, name: col_name }) in
                    table_items.iter().enumerate()
                {
                    let needs_comma = idx < (table_items.len() - 1);
                    let (schema_prefix, ty_resolved) = match &ty.metadata.return_sql {
                        Ok(Returns::One(SqlMapping::As(sql))) => (
                            context.schema_prefix_for_used_type(
                                &self_index,
                                "table return column",
                                ty,
                            )?,
                            sql.clone(),
                        ),
                        Ok(Returns::One(
                            mapping @ (SqlMapping::Composite | SqlMapping::Array(_)),
                        )) => (
                            context.schema_prefix_for_used_type(
                                &self_index,
                                "table return column",
                                ty,
                            )?,
                            sql_type(mapping, ty.composite_type)?,
                        ),
                        Ok(other) => {
                            return Err(eyre!(
                                "Got non-scalar table return item SQL in what macro-expansion thought was a table, got: {other:?}"
                            ));
                        }
                        Err(err) => return Err(*err).wrap_err("Error mapping return SQL"),
                    };
                    let item = format!(
                        "\n\t{col_name} {schema_prefix}{ty_resolved}{needs_comma} /* {ty_name} */",
                        col_name = col_name.expect(
                            "An iterator of tuples should have `named!()` macro declarations."
                        ),
                        schema_prefix = schema_prefix,
                        ty_resolved = ty_resolved,
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
                    .map(|attr| {
                        if matches!(attr, ExternArgs::Support(..)) {
                            let support_fn_name = attr.to_string();

                            let support_fn_name =
                            if let Some(entity) = context.find_matching_fn(&support_fn_name) {
                                entity.sql_name(context)
                            } else {
                                panic!("cannot locate SUPPORT function `{support_fn_name}` attached to function `{}`", self.full_path)
                            };

                            format!("SUPPORT {support_fn_name}")
                        } else {
                            attr.to_string().to_uppercase()
                        }
                    })
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
                    ExternArgs::Requires(requirements) => Some(requirements.clone()),
                    ExternArgs::Support(support_fn) => Some(vec![support_fn.clone()]),
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

            let left_arg = self
                .fn_args
                .first()
                .ok_or_else(|| eyre!("Did not find `left_arg` for operator `{}`.", self.name))?;
            let left_arg_sql = render_used_type_sql(
                context,
                self_index,
                "operator left argument",
                &left_arg.used_ty,
            )?;

            let right_arg = self
                .fn_args
                .get(1)
                .ok_or_else(|| eyre!("Did not find `left_arg` for operator `{}`.", self.name))?;
            let right_arg_sql = render_used_type_sql(
                context,
                self_index,
                "operator right argument",
                &right_arg.used_ty,
            )?;

            let schema = self
                .schema
                .map(|schema| format!("{schema}."))
                .unwrap_or_else(|| context.schema_prefix_for(&self_index));

            let operator_sql = format!(
                "\n\n\
                                                    -- {file}:{line}\n\
                                                    -- {module_path}::{name}\n\
                                                    CREATE OPERATOR {schema}{opname} (\n\
                                                        \tPROCEDURE={schema}\"{name}\",\n\
                                                        \tLEFTARG={left_arg_sql}, /* {left_name} */\n\
                                                        \tRIGHTARG={right_arg_sql}{maybe_comma} /* {right_name} */\n\
                                                        {optionals}\
                                                    );\
                                                    ",
                opname = op.opname.unwrap(),
                left_name = left_arg.used_ty.full_path,
                right_name = right_arg.used_ty.full_path,
                maybe_comma = if !optionals.is_empty() { "," } else { "" },
                optionals = if !optionals.is_empty() {
                    optionals.join(",\n") + "\n"
                } else {
                    "".to_string()
                },
            );
            ext_sql += &operator_sql
        };
        if let Some(cast) = &self.cast {
            let target_ty = match &self.fn_return {
                PgExternReturnEntity::Type { ty } => ty,
                other => {
                    return Err(eyre!("Casts must return a plain type, got: {other:?}"));
                }
            };
            let target_arg_sql = render_function_return_type(context, self_index, self)?;
            let source_arg = self
                .fn_args
                .first()
                .ok_or_else(|| eyre!("Did not find source type for cast `{}`.", self.name))?;
            let source_arg_sql =
                render_used_type_sql(context, self_index, "cast source type", &source_arg.used_ty)?;
            let optional = match cast {
                PgCastEntity::Default => String::from(""),
                PgCastEntity::Assignment => String::from(" AS ASSIGNMENT"),
                PgCastEntity::Implicit => String::from(" AS IMPLICIT"),
            };

            let cast_sql = format!(
                "\n\n\
                                                    -- {file}:{line}\n\
                                                    -- {module_path}::{name}\n\
                                                    CREATE CAST (\n\
                                                        \t{source_arg_sql} /* {source_name} */\n\
                                                        \tAS\n\
                                                        \t{target_arg_sql} /* {target_name} */\n\
                                                    )\n\
                                                    WITH FUNCTION {function_name}{optional};\
                                                    ",
                file = self.file,
                line = self.line,
                name = self.name,
                module_path = self.module_path,
                source_name = source_arg.used_ty.full_path,
                target_name = target_ty.full_path,
                function_name = self.name,
            );
            ext_sql += &cast_sql
        };
        Ok(ext_sql)
    }
}
