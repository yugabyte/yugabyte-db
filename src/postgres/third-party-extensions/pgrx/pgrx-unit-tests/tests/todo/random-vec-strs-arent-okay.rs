use pgrx::prelude::*;

fn main() {}

#[pg_extern]
fn exec<'a>(
    command: &'a str,
    args: default!(Vec<Option<&'a str>>, "ARRAY[]::text[]"),
) -> TableIterator<'static, (name!(status, Option<i32>), name!(stdout, String))> {
    let mut command = &mut std::process::Command::new(command);

    for arg in args {
        if let Some(arg) = arg {
            command = command.arg(arg);
        }
    }

    let output = command.output().expect("command failed");

    if !output.stderr.is_empty() {
        panic!("{}", String::from_utf8(output.stderr).expect("stderr is not valid utf8"))
    }

    TableIterator::once((
        output.status.code(),
        String::from_utf8(output.stdout).expect("stdout is not valid utf8"),
    ))
}
