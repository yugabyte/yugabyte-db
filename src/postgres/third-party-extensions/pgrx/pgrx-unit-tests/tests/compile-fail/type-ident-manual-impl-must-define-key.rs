use pgrx::pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable, TypeOrigin,
};

struct ManualTypeWithoutTypeIdent;

unsafe impl SqlTranslatable for ManualTypeWithoutTypeIdent {
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("manual_type_without_type_ident"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("manual_type_without_type_ident")));
}

fn main() {}
