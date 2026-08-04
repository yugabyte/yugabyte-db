use std::sync::Arc;

use arrow::array::{ArrayRef, BooleanArray, ListArray};

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Bool
impl PgTypeToArrowArray<bool> for Vec<Option<bool>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let bool_array = BooleanArray::from(self);
        Arc::new(bool_array)
    }
}

// Bool[]
impl PgTypeToArrowArray<bool> for Vec<Option<Vec<Option<bool>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let bool_array = BooleanArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(bool_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
