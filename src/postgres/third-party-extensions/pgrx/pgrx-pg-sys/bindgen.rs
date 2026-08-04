// not a build.rs so that it doesn't inherit the git history of the build.rs

// little-known Rust quirk: you can import main from wherever
use pgrx_bindgen::build::main;
