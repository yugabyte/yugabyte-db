use std::sync::Arc;

use arrow::array::{ArrayRef, FixedSizeBinaryArray, ListArray};
use pgrx::Uuid;

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Uuid
impl PgTypeToArrowArray<Uuid> for Vec<Option<Uuid>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let uuids = self
            .iter()
            .map(|uuid| uuid.as_ref().map(|uuid| uuid.as_bytes().as_slice()))
            .collect::<Vec<_>>();
        let uuid_array = FixedSizeBinaryArray::from(uuids);
        Arc::new(uuid_array)
    }
}

// Uuid[]
impl PgTypeToArrowArray<Uuid> for Vec<Option<Vec<Option<Uuid>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .iter()
            .flatten()
            .flatten()
            .map(|uuid| uuid.as_ref().map(|uuid| uuid.as_bytes().as_slice()))
            .collect::<Vec<_>>();

        let uuid_array = FixedSizeBinaryArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(uuid_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
