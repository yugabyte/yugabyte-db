use arrow::array::{Array, Int64Array};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Int8
impl ArrowArrayToPgType<i64> for Int64Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<i64> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Int8[]
impl ArrowArrayToPgType<Vec<Option<i64>>> for Int64Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<i64>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }
        Some(vals)
    }
}
