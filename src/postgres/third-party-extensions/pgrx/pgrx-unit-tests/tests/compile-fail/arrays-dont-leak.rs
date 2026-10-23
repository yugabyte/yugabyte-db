use pgrx::prelude::*;

fn main() {}

#[pg_extern]
fn array_echo_a<'a>(a: Array<'a, &'a str>) -> Vec<Option<&'a str>> {
    let v = a.iter().collect();
    drop(a);
    v
}

#[pg_extern]
fn array_echo_aba<'a, 'b>(a: Array<'a, &'b str>) -> Vec<Option<&'a str>> {
    let v = a.iter().collect();
    drop(a);
    v
}

#[pg_extern]
fn array_echo_baa<'a, 'b>(a: Array<'b, &'a str>) -> Vec<Option<&'a str>> {
    let v = a.iter().collect();
    drop(a);
    v
}

fn test_leak_after_drop() -> Result<(), Box<dyn std::error::Error>> {
    Spi::run("create table test_leak_after_drop (a text[]);")?;
    Spi::run(
        "insert into test_leak_after_drop (a) select array_agg(x::text) from generate_series(1, 10000) x;",
    )?;
    let array = Spi::get_one::<Array<&str>>("SELECT array_echo(a) FROM test_leak_after_drop")?
        .expect("datum was null");
    let top_5 = array.iter().take(5).collect::<Vec<_>>();
    drop(array);

    // just check the top 5 values.  Even the first will be wrong if the backing Array data is freed
    assert_eq!(top_5, &[Some("1"), Some("2"), Some("3"), Some("4"), Some("5")]);
    Ok(())
}
