use arrow::array::{Array, Time64MicrosecondArray};
use pgrx::datum::TimeWithTimeZone;

use crate::type_compat::pg_arrow_type_conversions::i64_to_timetz;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Timetz
impl ArrowArrayToPgType<TimeWithTimeZone> for Time64MicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<TimeWithTimeZone> {
        if self.is_null(0) {
            None
        } else {
            Some(i64_to_timetz(self.value(0)))
        }
    }
}

// Timetz[]
impl ArrowArrayToPgType<Vec<Option<TimeWithTimeZone>>> for Time64MicrosecondArray {
    fn to_pg_type(
        self,
        _context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<TimeWithTimeZone>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(i64_to_timetz);
            vals.push(val);
        }
        Some(vals)
    }
}
