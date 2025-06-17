use arrow::array::{Array, UInt32Array};
use pgrx::pg_sys::Oid;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Oid
impl ArrowArrayToPgType<Oid> for UInt32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Oid> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val.into())
        }
    }
}

// Oid[]
impl ArrowArrayToPgType<Vec<Option<Oid>>> for UInt32Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Oid>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| val.into());
            vals.push(val);
        }
        Some(vals)
    }
}
