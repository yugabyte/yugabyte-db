use std::process::{Command, ExitCode};

use std::fs;
use std::path::PathBuf;

fn main() -> Result<ExitCode, Box<dyn std::error::Error>> {
    println!("cargo:rerun-if-changed=c_ext/c_ext.c");
    println!("cargo:rerun-if-changed=c_ext/Makefile");

    let target = std::env::var("TARGET")?;
    if target.contains("windows") {
        println!("cargo:warning=skipping pgthread c_ext build on windows");
        return Ok(ExitCode::SUCCESS);
    }

    let (_, pg_config) =
        pgrx_bindgen::detect_pg_config()?.pop().unwrap_or_else(|| panic!("no pg_config detected"));

    let child = Command::new("make")
        .current_dir("c_ext")
        .env("USE_PGXS", "1")
        .env("PROFILE", "")
        .env("PG_CONFIG", pg_config.path().expect("pg_config must have a path"))
        .spawn()?;
    let output = child.wait_with_output()?;
    if !output.status.success() {
        eprintln!("{}", std::str::from_utf8(&output.stderr)?);
        return Ok(ExitCode::from(output.status.code().unwrap() as u8));
    }

    let output = std::env::current_dir()?.join("c_ext");
    println!("cargo:rustc-link-arg-cdylib={}/c_ext.o", output.display());

    let out_dir = PathBuf::from(std::env::var("OUT_DIR")?);

    // Force-export these symbols from the final .so
    let vers = out_dir.join("exports.map");
    fs::write(
        &vers,
        r#"
{
  global:
    start_thread;
    pg_finfo_start_thread;
  local:
    *;
};
"#,
    )?;

    if target.contains("apple-darwin") {
        println!("cargo:rustc-link-arg-cdylib=-Wl,-u,_start_thread");
        println!("cargo:rustc-link-arg-cdylib=-Wl,-exported_symbol,_start_thread");
        println!("cargo:rustc-link-arg-cdylib=-Wl,-exported_symbol,_pg_finfo_start_thread");
    } else if target.contains("unknown-linux-gnu") {
        println!("cargo:rustc-link-arg-cdylib=-Wl,--undefined=start_thread");
        println!("cargo:rustc-link-arg-cdylib=-Wl,--undefined=pg_finfo_start_thread");
        println!("cargo:rustc-link-arg-cdylib=-Wl,--version-script={}", vers.display());
    }

    Ok(ExitCode::SUCCESS)
}
