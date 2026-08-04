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

`#[pg_aggregate]` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/
use crate::aggregate::options::{FinalizeModify, ParallelOption};
use crate::fmt;
use crate::metadata::SqlMapping;
use crate::pgrx_sql::PgrxSql;
use crate::to_sql::entity::ToSqlConfigEntity;
use crate::to_sql::ToSql;
use crate::type_keyed;
use crate::{SqlGraphEntity, SqlGraphIdentifier, UsedTypeEntity};
use core::any::TypeId;
use eyre::{eyre, WrapErr};

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct AggregateTypeEntity {
    pub used_ty: UsedTypeEntity,
    pub name: Option<&'static str>,
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct PgAggregateEntity {
    pub full_path: &'static str,
    pub module_path: &'static str,
    pub file: &'static str,
    pub line: u32,
    pub ty_id: TypeId,

    pub name: &'static str,

    /// If the aggregate is an ordered set aggregate.
    ///
    /// See [the PostgreSQL ordered set docs](https://www.postgresql.org/docs/current/xaggr.html#XAGGR-ORDERED-SET-AGGREGATES).
    pub ordered_set: bool,

    /// The `arg_data_type` list.
    ///
    /// Corresponds to `Args` in `pgrx::aggregate::Aggregate`.
    pub args: Vec<AggregateTypeEntity>,

    /// The direct argument list, appearing before `ORDER BY` in ordered set aggregates.
    ///
    /// Corresponds to `OrderBy` in `pgrx::aggregate::Aggregate`.
    pub direct_args: Option<Vec<AggregateTypeEntity>>,

    /// The `STYPE` and `name` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// The implementor of an `pgrx::aggregate::Aggregate`.
    pub stype: AggregateTypeEntity,

    /// The `SFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `state` in `pgrx::aggregate::Aggregate`.
    pub sfunc: &'static str,

    /// The `FINALFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `finalize` in `pgrx::aggregate::Aggregate`.
    pub finalfunc: Option<&'static str>,

    /// The `FINALFUNC_MODIFY` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `FINALIZE_MODIFY` in `pgrx::aggregate::Aggregate`.
    pub finalfunc_modify: Option<FinalizeModify>,

    /// The `COMBINEFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `combine` in `pgrx::aggregate::Aggregate`.
    pub combinefunc: Option<&'static str>,

    /// The `SERIALFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `serial` in `pgrx::aggregate::Aggregate`.
    pub serialfunc: Option<&'static str>,

    /// The `DESERIALFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `deserial` in `pgrx::aggregate::Aggregate`.
    pub deserialfunc: Option<&'static str>,

    /// The `INITCOND` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `INITIAL_CONDITION` in `pgrx::aggregate::Aggregate`.
    pub initcond: Option<&'static str>,

    /// The `MSFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `moving_state` in `pgrx::aggregate::Aggregate`.
    pub msfunc: Option<&'static str>,

    /// The `MINVFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `moving_state_inverse` in `pgrx::aggregate::Aggregate`.
    pub minvfunc: Option<&'static str>,

    /// The `MSTYPE` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `MovingState` in `pgrx::aggregate::Aggregate`.
    pub mstype: Option<UsedTypeEntity>,

    // The `MSSPACE` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    //
    // TODO: Currently unused.
    // pub msspace: &'static str,
    /// The `MFINALFUNC` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `moving_state_finalize` in `pgrx::aggregate::Aggregate`.
    pub mfinalfunc: Option<&'static str>,

    /// The `MFINALFUNC_MODIFY` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `MOVING_FINALIZE_MODIFY` in `pgrx::aggregate::Aggregate`.
    pub mfinalfunc_modify: Option<FinalizeModify>,

    /// The `MINITCOND` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `MOVING_INITIAL_CONDITION` in `pgrx::aggregate::Aggregate`.
    pub minitcond: Option<&'static str>,

    /// The `SORTOP` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `SORT_OPERATOR` in `pgrx::aggregate::Aggregate`.
    pub sortop: Option<&'static str>,

    /// The `PARALLEL` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `PARALLEL` in `pgrx::aggregate::Aggregate`.
    pub parallel: Option<ParallelOption>,

    /// The `HYPOTHETICAL` parameter for [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
    ///
    /// Corresponds to `hypothetical` in `pgrx::aggregate::Aggregate`.
    pub hypothetical: bool,
    pub to_sql_config: ToSqlConfigEntity,
}

impl From<PgAggregateEntity> for SqlGraphEntity {
    fn from(val: PgAggregateEntity) -> Self {
        SqlGraphEntity::Aggregate(val)
    }
}

impl SqlGraphIdentifier for PgAggregateEntity {
    fn dot_identifier(&self) -> String {
        format!("aggregate {}", self.full_path)
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

impl ToSql for PgAggregateEntity {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String> {
        let self_index = context.aggregates[self];
        let mut optional_attributes = Vec::new();
        let schema = context.schema_prefix_for(&self_index);

        if let Some(value) = self.finalfunc {
            optional_attributes.push((
                format!("\tFINALFUNC = {schema}\"{value}\""),
                format!("/* {}::final */", self.full_path),
            ));
        }
        if let Some(value) = self.finalfunc_modify {
            optional_attributes.push((
                format!("\tFINALFUNC_MODIFY = {}", value.to_sql(context)?),
                format!("/* {}::FINALIZE_MODIFY */", self.full_path),
            ));
        }
        if let Some(value) = self.combinefunc {
            optional_attributes.push((
                format!("\tCOMBINEFUNC = {schema}\"{value}\""),
                format!("/* {}::combine */", self.full_path),
            ));
        }
        if let Some(value) = self.serialfunc {
            optional_attributes.push((
                format!("\tSERIALFUNC = {schema}\"{value}\""),
                format!("/* {}::serial */", self.full_path),
            ));
        }
        if let Some(value) = self.deserialfunc {
            optional_attributes.push((
                format!("\tDESERIALFUNC ={schema} \"{value}\""),
                format!("/* {}::deserial */", self.full_path),
            ));
        }
        if let Some(value) = self.initcond {
            optional_attributes.push((
                format!("\tINITCOND = '{value}'"),
                format!("/* {}::INITIAL_CONDITION */", self.full_path),
            ));
        }
        if let Some(value) = self.msfunc {
            optional_attributes.push((
                format!("\tMSFUNC = {schema}\"{value}\""),
                format!("/* {}::moving_state */", self.full_path),
            ));
        }
        if let Some(value) = self.minvfunc {
            optional_attributes.push((
                format!("\tMINVFUNC = {schema}\"{value}\""),
                format!("/* {}::moving_state_inverse */", self.full_path),
            ));
        }
        if let Some(value) = self.mfinalfunc {
            optional_attributes.push((
                format!("\tMFINALFUNC = {schema}\"{value}\""),
                format!("/* {}::moving_state_finalize */", self.full_path),
            ));
        }
        if let Some(value) = self.mfinalfunc_modify {
            optional_attributes.push((
                format!("\tMFINALFUNC_MODIFY = {}", value.to_sql(context)?),
                format!("/* {}::MOVING_FINALIZE_MODIFY */", self.full_path),
            ));
        }
        if let Some(value) = self.minitcond {
            optional_attributes.push((
                format!("\tMINITCOND = '{value}'"),
                format!("/* {}::MOVING_INITIAL_CONDITION */", self.full_path),
            ));
        }
        if let Some(value) = self.sortop {
            optional_attributes.push((
                format!("\tSORTOP = \"{value}\""),
                format!("/* {}::SORT_OPERATOR */", self.full_path),
            ));
        }
        if let Some(value) = self.parallel {
            optional_attributes.push((
                format!("\tPARALLEL = {}", value.to_sql(context)?),
                format!("/* {}::PARALLEL */", self.full_path),
            ));
        }
        if self.hypothetical {
            optional_attributes.push((
                String::from("\tHYPOTHETICAL"),
                format!("/* {}::hypothetical */", self.full_path),
            ))
        }

        let map_ty = |used_ty: &UsedTypeEntity| -> eyre::Result<String> {
            match used_ty.metadata.argument_sql {
                Ok(SqlMapping::As(ref argument_sql)) => Ok(argument_sql.to_string()),
                Ok(SqlMapping::Composite { array_brackets }) => used_ty
                    .composite_type
                    .map(|v| fmt::with_array_brackets(v.into(), array_brackets))
                    .ok_or_else(|| {
                        eyre!("Macro expansion time suggested a composite_type!() in return")
                    }),
                Ok(SqlMapping::Skip) => {
                    Err(eyre!("Cannot use skipped SQL translatable type as aggregate const type"))
                }
                Err(err) => Err(err).wrap_err("While mapping argument"),
            }
        };

        let stype_sql = map_ty(&self.stype.used_ty).wrap_err("Mapping state type")?;
        let stype_schema = context
            .types
            .iter()
            .map(type_keyed)
            .chain(context.enums.iter().map(type_keyed))
            .find(|(ty, _)| ty.id_matches(&self.stype.used_ty.ty_id))
            .map(|(_, ty_index)| context.schema_prefix_for(ty_index))
            .unwrap_or_default();

        if let Some(value) = &self.mstype {
            let mstype_sql = map_ty(value).wrap_err("Mapping moving state type")?;
            optional_attributes.push((
                format!("\tMSTYPE = {mstype_sql}"),
                format!("/* {}::MovingState = {} */", self.full_path, value.full_path),
            ));
        }

        let mut optional_attributes_string = String::new();
        for (index, (optional_attribute, comment)) in optional_attributes.iter().enumerate() {
            let optional_attribute_string = format!(
                "{optional_attribute}{maybe_comma} {comment}{maybe_newline}",
                optional_attribute = optional_attribute,
                maybe_comma = if index == optional_attributes.len() - 1 { "" } else { "," },
                comment = comment,
                maybe_newline = if index == optional_attributes.len() - 1 { "" } else { "\n" }
            );
            optional_attributes_string += &optional_attribute_string;
        }

        let args = {
            let mut args = Vec::new();
            for (idx, arg) in self.args.iter().enumerate() {
                let graph_index = context
                    .graph
                    .neighbors_undirected(self_index)
                    .find(|neighbor| context.graph[*neighbor].type_matches(&arg.used_ty))
                    .ok_or_else(|| {
                        eyre!("Could not find arg type in graph. Got: {:?}", arg.used_ty)
                    })?;
                let needs_comma = idx < (self.args.len() - 1);
                let buf = format!("\
                       \t{name}{variadic}{schema_prefix}{sql_type}{maybe_comma}/* {full_path} */\
                   ",
                       schema_prefix = context.schema_prefix_for(&graph_index),
                       // First try to match on [`TypeId`] since it's most reliable.
                       sql_type = match arg.used_ty.metadata.argument_sql {
                            Ok(SqlMapping::As(ref argument_sql)) => {
                                argument_sql.to_string()
                            }
                            Ok(SqlMapping::Composite {
                                array_brackets,
                            }) => {
                                arg.used_ty
                                    .composite_type
                                    .map(|v| {
                                        fmt::with_array_brackets(v.into(), array_brackets)
                                    })
                                    .ok_or_else(|| {
                                        eyre!(
                                        "Macro expansion time suggested a composite_type!() in return"
                                    )
                                    })?
                            }
                            Ok(SqlMapping::Skip) => return Err(eyre!("Got a skipped SQL translatable type in aggregate args, this is not permitted")),
                            Err(err) => return Err(err).wrap_err("While mapping argument")
                        },
                       variadic = if arg.used_ty.variadic { "VARIADIC " } else { "" },
                       maybe_comma = if needs_comma { ", " } else { " " },
                       full_path = arg.used_ty.full_path,
                       name = if let Some(name) = arg.name {
                           format!(r#""{name}" "#)
                       } else { "".to_string() },
                );
                args.push(buf);
            }
            "\n".to_string() + &args.join("\n") + "\n"
        };
        let direct_args = if let Some(direct_args) = &self.direct_args {
            let mut args = Vec::new();
            for (idx, arg) in direct_args.iter().enumerate() {
                let graph_index = context
                    .graph
                    .neighbors_undirected(self_index)
                    .find(|neighbor| context.graph[*neighbor].type_matches(&arg.used_ty))
                    .ok_or_else(|| eyre!("Could not find arg type in graph. Got: {:?}", arg))?;
                let needs_comma = idx < (direct_args.len() - 1);
                let buf = format!(
                    "\
                    \t{maybe_name}{schema_prefix}{sql_type}{maybe_comma}/* {full_path} */\
                   ",
                    schema_prefix = context.schema_prefix_for(&graph_index),
                    // First try to match on [`TypeId`] since it's most reliable.
                    sql_type = map_ty(&arg.used_ty).wrap_err("Mapping direct arg type")?,
                    maybe_name = if let Some(name) = arg.name {
                        "\"".to_string() + name + "\" "
                    } else {
                        "".to_string()
                    },
                    maybe_comma = if needs_comma { ", " } else { " " },
                    full_path = arg.used_ty.full_path,
                );
                args.push(buf);
            }
            "\n".to_string() + &args.join("\n") + "\n"
        } else {
            String::default()
        };

        let PgAggregateEntity { name, full_path, file, line, sfunc, .. } = self;

        let sql = format!(
            "\n\
                -- {file}:{line}\n\
                -- {full_path}\n\
                CREATE AGGREGATE {schema}{name} ({direct_args}{maybe_order_by}{args})\n\
                (\n\
                    \tSFUNC = {schema}\"{sfunc}\", /* {full_path}::state */\n\
                    \tSTYPE = {stype_schema}{stype_sql}{maybe_comma_after_stype} /* {stype_full_path} */\
                    {optional_attributes}\
                );\
            ",
            stype_full_path = self.stype.used_ty.full_path,
            maybe_comma_after_stype = if optional_attributes.is_empty() { "" } else { "," },
            maybe_order_by = if self.ordered_set { "\tORDER BY" } else { "" },
            optional_attributes = String::from("\n")
                + &optional_attributes_string
                + if optional_attributes.is_empty() { "" } else { "\n" },
        );
        Ok(sql)
    }
}
