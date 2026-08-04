use pgrx::prelude::*;
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PostgresType, PostgresHash)]
pub struct BrokenType {
    int: i32,
}

fn main() {}
