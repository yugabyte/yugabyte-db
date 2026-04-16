use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, StringArray};
use pgrx::Json;

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Json
impl PgTypeToArrowArray<Json> for Vec<Option<Json>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let jsons = self
            .into_iter()
            .map(|json| {
                json.map(|json| {
                    serde_json::to_string(&json)
                        .unwrap_or_else(|e| panic!("failed to serialize JSON value: {}", e))
                })
            })
            .collect::<Vec<_>>();

        let json_array = StringArray::from(jsons);
        Arc::new(json_array)
    }
}

// Json[]
impl PgTypeToArrowArray<Json> for Vec<Option<Vec<Option<Json>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|json| {
                json.map(|json| {
                    serde_json::to_string(&json)
                        .unwrap_or_else(|e| panic!("failed to serialize JSON value: {}", e))
                })
            })
            .collect::<Vec<_>>();

        let json_array = StringArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(json_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
