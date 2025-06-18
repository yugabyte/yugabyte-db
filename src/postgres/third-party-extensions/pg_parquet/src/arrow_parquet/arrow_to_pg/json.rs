use arrow::array::{Array, StringArray};
use pgrx::Json;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Json
impl ArrowArrayToPgType<Json> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Json> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(Json(
                serde_json::from_str(val).unwrap_or_else(|_| panic!("invalid json: {}", val)),
            ))
        }
    }
}

// Json[]
impl ArrowArrayToPgType<Vec<Option<Json>>> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Json>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| {
                Json(serde_json::from_str(val).unwrap_or_else(|_| panic!("invalid json: {}", val)))
            });
            vals.push(val);
        }
        Some(vals)
    }
}
