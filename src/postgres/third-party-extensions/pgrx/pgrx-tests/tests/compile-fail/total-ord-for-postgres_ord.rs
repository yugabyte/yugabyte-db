use pgrx::prelude::*;
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PartialEq, PartialOrd, PostgresType, PostgresOrd)]
pub struct BrokenType {
    int: i32,
}

impl BrokenType {
    fn cmp(self, _other: &Self) -> Anything {
        Anything::Whatever
    }
}

#[repr(u8)]
enum Anything {
    Whatever = 0,
}

fn main() {}
