use arrow::array::{Array, FixedSizeBinaryArray};
use pgrx::Uuid;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Uuid
impl ArrowArrayToPgType<Uuid> for FixedSizeBinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Uuid> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(Uuid::from_slice(val).expect("Invalid Uuid"))
        }
    }
}

// Uuid[]
impl ArrowArrayToPgType<Vec<Option<Uuid>>> for FixedSizeBinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Uuid>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val.map(|val| Uuid::from_slice(val).expect("Invalid Uuid")));
        }
        Some(vals)
    }
}
