use pgrx::prelude::*;

#[pg_extern]
fn split(input: &'static str, pattern: &str) -> Vec<&'static str> {
    input.split_terminator(pattern).collect()
}

fn main() {}
