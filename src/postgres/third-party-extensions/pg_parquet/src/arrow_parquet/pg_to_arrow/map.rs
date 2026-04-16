use std::sync::Arc;

use arrow::array::{ArrayRef, AsArray, ListArray, MapArray};

use crate::{
    arrow_parquet::{
        arrow_utils::{arrow_array_offsets, arrow_map_offsets},
        pg_to_arrow::PgTypeToArrowArray,
    },
    type_compat::map::Map,
};

use super::PgToArrowAttributeContext;

// crunchy_map.key_<type1>_val_<type2>
impl<'b> PgTypeToArrowArray<Map<'b>> for Vec<Option<Map<'b>>> {
    fn to_arrow_array(self, context: &PgToArrowAttributeContext) -> ArrayRef {
        let (map_offsets, map_nulls) = arrow_map_offsets(&self);

        let maps = self;

        let mut entries = vec![];

        for map in &maps {
            if let Some(map) = map {
                entries.push(Some(map.entries.iter().collect::<Vec<_>>()));
            } else {
                entries.push(None)
            };
        }

        let entries_context = context.entries_context();

        let entries = entries.into_iter().flatten().flatten().collect::<Vec<_>>();

        let entries_array = entries.to_arrow_array(entries_context);
        let entries_array = entries_array.as_struct().to_owned();

        let map_array = MapArray::new(
            entries_context.field(),
            map_offsets,
            entries_array,
            Some(map_nulls),
            false,
        );

        Arc::new(map_array)
    }
}

// crunchy_map.key_<type1>_val_<type2>[]
impl<'b> PgTypeToArrowArray<Map<'b>> for Vec<Option<Vec<Option<Map<'b>>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (list_offsets, list_nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let maps = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let map_field = element_context.field();

        let map_array = maps.to_arrow_array(element_context);

        let list_array = ListArray::new(map_field, list_offsets, map_array, Some(list_nulls));

        Arc::new(list_array)
    }
}
