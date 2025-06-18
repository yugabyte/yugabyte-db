use arrow::array::{Array, Time64MicrosecondArray};
use pgrx::datum::Time;

use crate::type_compat::pg_arrow_type_conversions::i64_to_time;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Time
impl ArrowArrayToPgType<Time> for Time64MicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Time> {
        if self.is_null(0) {
            None
        } else {
            Some(i64_to_time(self.value(0)))
        }
    }
}

// Time[]
impl ArrowArrayToPgType<Vec<Option<Time>>> for Time64MicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Time>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(i64_to_time);
            vals.push(val);
        }
        Some(vals)
    }
}
