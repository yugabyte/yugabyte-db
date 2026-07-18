use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, StringArray};

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Char
impl PgTypeToArrowArray<i8> for Vec<Option<i8>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let chars = self
            .into_iter()
            .map(|c| c.map(|c| (c as u8 as char).to_string()))
            .collect::<Vec<_>>();
        let char_array = StringArray::from(chars);
        Arc::new(char_array)
    }
}

// "Char"[]
impl PgTypeToArrowArray<i8> for Vec<Option<Vec<Option<i8>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|c| c.map(|c| (c as u8 as char).to_string()))
            .collect::<Vec<_>>();

        let char_array = StringArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(char_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
