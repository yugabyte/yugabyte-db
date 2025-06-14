use arrow::array::{Array, Date32Array};
use pgrx::datum::Date;

use crate::type_compat::pg_arrow_type_conversions::i32_to_date;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Date
impl ArrowArrayToPgType<Date> for Date32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Date> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(i32_to_date(val))
        }
    }
}

// Date[]
impl ArrowArrayToPgType<Vec<Option<Date>>> for Date32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Date>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(i32_to_date);
            vals.push(val);
        }
        Some(vals)
    }
}
