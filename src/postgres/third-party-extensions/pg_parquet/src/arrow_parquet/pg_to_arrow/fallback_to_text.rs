use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, StringArray};

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::fallback_to_text::FallbackToText,
};

use super::PgToArrowAttributeContext;

// Text representation of any type
impl PgTypeToArrowArray<FallbackToText> for Vec<Option<FallbackToText>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let texts = self
            .into_iter()
            .map(|f| f.map(String::from))
            .collect::<Vec<_>>();
        let text_array = StringArray::from(texts);
        Arc::new(text_array)
    }
}

// Text[] representation of any type
impl PgTypeToArrowArray<FallbackToText> for Vec<Option<Vec<Option<FallbackToText>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|f| f.map(String::from))
            .collect::<Vec<_>>();

        let text_array = StringArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(text_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
