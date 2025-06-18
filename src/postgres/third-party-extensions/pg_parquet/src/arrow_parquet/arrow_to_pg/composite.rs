use arrow::array::{Array, StructArray};
use pgrx::{prelude::PgHeapTuple, AllocatedByRust};

use super::{to_pg_datum, ArrowArrayToPgType, ArrowToPgAttributeContext};

// PgHeapTuple
impl<'a> ArrowArrayToPgType<PgHeapTuple<'a, AllocatedByRust>> for StructArray {
    fn to_pg_type(
        self,
        context: &ArrowToPgAttributeContext,
    ) -> Option<PgHeapTuple<'a, AllocatedByRust>> {
        if self.is_null(0) {
            return None;
        }

        let mut datums = vec![];

        for attribute_context in context.attribute_contexts() {
            let column_data = self
                .column_by_name(attribute_context.name())
                .unwrap_or_else(|| panic!("column {} not found", &attribute_context.name()));

            let datum = to_pg_datum(column_data.into_data(), attribute_context);

            datums.push(datum);
        }

        let tupledesc = context.tupledesc();

        Some(
            unsafe { PgHeapTuple::from_datums(tupledesc.clone(), datums) }.unwrap_or_else(|e| {
                panic!("failed to create heap tuple: {}", e);
            }),
        )
    }
}

// PgHeapTuple[]
impl<'a> ArrowArrayToPgType<Vec<Option<PgHeapTuple<'a, AllocatedByRust>>>> for StructArray {
    fn to_pg_type(
        self,
        element_context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<PgHeapTuple<'a, AllocatedByRust>>>> {
        let len = self.len();
        let mut values = Vec::with_capacity(len);

        for i in 0..len {
            let tuple = self.slice(i, 1);

            let tuple = tuple.to_pg_type(element_context);

            values.push(tuple);
        }

        Some(values)
    }
}
