use arrow::array::{Array, Int32Array};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Int4
impl ArrowArrayToPgType<i32> for Int32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<i32> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Int4[]
impl ArrowArrayToPgType<Vec<Option<i32>>> for Int32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<i32>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }
        Some(vals)
    }
}
