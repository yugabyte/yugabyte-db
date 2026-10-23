use pgrx::prelude::*;
use std::error::Error;

#[pg_test]
#[allow(deprecated)]
fn issue1209() -> Result<Option<String>, Box<dyn Error>> {
    // create the cursor we actually care about
    let mut res = Spi::connect(|c| {
        c.open_cursor("select 'hello world' from generate_series(1, 1000)", &[])
            .fetch(1000)
            .unwrap()
    });

    // here we just perform some allocations to make sure that the previous cursor gets invalidated
    for _ in 0..100 {
        Spi::connect(|c| c.open_cursor("select 1", &[]).fetch(1).unwrap());
    }

    // later elements are probably more likely to point to deallocated memory
    for _ in 0..100 {
        res.next();
    }

    // segfault
    Ok(res.next().unwrap().get::<String>(1)?)
}

fn main() {}
