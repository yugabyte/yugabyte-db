use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, StringArray};
use pgrx::JsonB;

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// JsonB
impl PgTypeToArrowArray<JsonB> for Vec<Option<JsonB>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let jsonbs = self
            .into_iter()
            .map(|jsonb| {
                jsonb.map(|jsonb| {
                    serde_json::to_string(&jsonb)
                        .unwrap_or_else(|e| panic!("failed to serialize jsonb value: {}", e))
                })
            })
            .collect::<Vec<_>>();

        let jsonb_array = StringArray::from(jsonbs);
        Arc::new(jsonb_array)
    }
}

// JsonB[]
impl PgTypeToArrowArray<JsonB> for Vec<Option<Vec<Option<JsonB>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|jsonb| {
                jsonb.map(|jsonb| {
                    serde_json::to_string(&jsonb)
                        .unwrap_or_else(|e| panic!("failed to serialize jsonb value: {}", e))
                })
            })
            .collect::<Vec<_>>();

        let jsonb_array = StringArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(jsonb_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
