use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, UInt32Array};
use pgrx::pg_sys::Oid;

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Oid
impl PgTypeToArrowArray<Oid> for Vec<Option<Oid>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let oids = self
            .into_iter()
            .map(|oid| oid.map(|oid| oid.to_u32()))
            .collect::<Vec<_>>();
        let oid_array = UInt32Array::from(oids);
        Arc::new(oid_array)
    }
}

// Oid[]
impl PgTypeToArrowArray<Oid> for Vec<Option<Vec<Option<Oid>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|oid| oid.map(|oid| oid.to_u32()))
            .collect::<Vec<_>>();

        let oid_array = UInt32Array::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(oid_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
