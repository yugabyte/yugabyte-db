use pgrx::prelude::*;
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PartialEq, PostgresType, PostgresEq)]
pub struct BrokenType {
    int: i32,
}

fn main() {}
