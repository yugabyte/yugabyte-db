use arrow::array::{Array, MapArray};
use pgrx::{prelude::PgHeapTuple, AllocatedByRust, FromDatum, IntoDatum};

use crate::type_compat::map::Map;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// crunchy_map.key_<type1>_val_<type2>
impl<'a> ArrowArrayToPgType<Map<'a>> for MapArray {
    fn to_pg_type(self, context: &ArrowToPgAttributeContext) -> Option<Map<'a>> {
        if self.is_null(0) {
            None
        } else {
            let entries_array = self.value(0);

            let entries: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> =
                entries_array.to_pg_type(context.entries_context());

            if let Some(entries) = entries {
                let entries_datum = entries.into_datum();

                if let Some(entries_datum) = entries_datum {
                    let entries = unsafe {
                        let is_null = false;
                        pgrx::Array::from_datum(entries_datum, is_null)
                            .expect("map entries should be an array")
                    };
                    Some(Map { entries })
                } else {
                    None
                }
            } else {
                None
            }
        }
    }
}

// crunchy_map.key_<type1>_val_<type2>[]
impl<'a> ArrowArrayToPgType<Vec<Option<Map<'a>>>> for MapArray {
    fn to_pg_type(
        self,
        element_context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<Map<'a>>>> {
        let mut maps = vec![];

        for entries_array in self.iter() {
            if let Some(entries_array) = entries_array {
                let entries: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> =
                    entries_array.to_pg_type(element_context.entries_context());

                if let Some(entries) = entries {
                    let entries_datum = entries.into_datum();

                    if let Some(entries_datum) = entries_datum {
                        let entries = unsafe {
                            let is_null = false;
                            pgrx::Array::from_datum(entries_datum, is_null)
                                .expect("map entries should be an array")
                        };
                        maps.push(Some(Map { entries }))
                    } else {
                        maps.push(None);
                    }
                } else {
                    maps.push(None);
                }
            } else {
                maps.push(None);
            }
        }

        Some(maps)
    }
}
