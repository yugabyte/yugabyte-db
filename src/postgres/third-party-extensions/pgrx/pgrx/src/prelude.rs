//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
// From "external" crates:
pub use ::pgrx_macros::*; // yes, we really do want to re-export every macro that pgrx_macros provides

// Some items previously available from pgrx_pg_sys are interposed by pgrx
pub use crate::pg_sys;

// Can't make an extension without this
pub use crate::pg_module_magic;

// Necessary local macros:
pub use crate::{default, impl_sql_translatable, name};

// Needed for variant RETURNS
pub use crate::iter::{SetOfIterator, TableIterator};

// Needed for complex returns and Triggers
pub use crate::heap_tuple::{PgHeapTuple, PgHeapTupleError};
pub use crate::pgbox::{AllocatedByPostgres, AllocatedByRust, PgBox, WhoAllocated};

// These could be factored into a temporal type module that could be easily imported for code which works with them.
// However, reexporting them seems fine for now.

pub use crate::inoutfuncs::{InOutFuncs, PgVarlenaInOutFuncs};
pub use crate::{
    datum::{
        AnyNumeric, Array, ArraySliceError, Date, FromDatum, Interval, IntoDatum, Numeric,
        PgVarlena, PostgresType, Range, RangeBound, RangeSubType, Time, TimeWithTimeZone,
        Timestamp, TimestampWithTimeZone, VariadicArray, datetime_support::*,
    },
    oids_of,
};

// Trigger support
pub use crate::trigger_support::{
    PgTrigger, PgTriggerError, PgTriggerLevel, PgTriggerOperation, PgTriggerWhen,
};

// Aggregate support
pub use crate::aggregate::{Aggregate, FinalizeModify, ParallelOption};

pub use crate::pg_sys::PgBuiltInOids;
pub use crate::pg_sys::oids::PgOid;
pub use crate::pg_sys::pg_try::PgTryBuilder;
pub use crate::pg_sys::utils::name_data_to_str;

// It's a database, gotta query it somehow.
pub use crate::spi;
pub use crate::spi::Spi;

// Logging and Error support
pub use crate::pg_sys::elog::PgLogLevel;
pub use crate::pg_sys::errcodes::PgSqlErrorCode;
pub use crate::pg_sys::{
    FATAL, PANIC, check_for_interrupts, debug1, debug2, debug3, debug4, debug5, ereport,
    ereport_domain, error, function_name, info, log, notice, warning,
};

#[cfg(test)]
mod tests {
    use crate::pgrx_sql_entity_graph::metadata::{
        ReturnsRef, SqlMappingRef, SqlTranslatable, TypeOrigin,
    };
    use crate::prelude::*;

    struct PreludeMacroType;
    impl_sql_translatable!(PreludeMacroType, "uuid");

    #[test]
    fn prelude_reexports_impl_sql_translatable() {
        assert_eq!(
            <PreludeMacroType as SqlTranslatable>::TYPE_IDENT,
            concat!(module_path!(), "::", "PreludeMacroType")
        );
        assert_eq!(<PreludeMacroType as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
        assert_eq!(
            <PreludeMacroType as SqlTranslatable>::ARGUMENT_SQL,
            Ok(SqlMappingRef::literal("uuid"))
        );
        assert_eq!(
            <PreludeMacroType as SqlTranslatable>::RETURN_SQL,
            Ok(ReturnsRef::One(SqlMappingRef::literal("uuid")))
        );
    }
}
