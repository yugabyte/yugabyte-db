use arrow::array::{Array, Float32Array};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Float4
impl ArrowArrayToPgType<f32> for Float32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<f32> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Float4[]
impl ArrowArrayToPgType<Vec<Option<f32>>> for Float32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<f32>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }
        Some(vals)
    }
}
