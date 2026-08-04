use arrow::array::{Array, Float64Array};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Float8
impl ArrowArrayToPgType<f64> for Float64Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<f64> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Float8[]
impl ArrowArrayToPgType<Vec<Option<f64>>> for Float64Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<f64>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }
        Some(vals)
    }
}
