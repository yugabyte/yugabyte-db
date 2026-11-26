#![cfg(not(target_env = "musl"))]

use trybuild::TestCases;

/// These are tests which are intended to always fail.
#[cfg(not(feature = "nightly"))]
#[test]
fn compile_fail() {
    let t = TestCases::new();
    t.compile_fail("tests/compile-fail/*.rs");
}

/// These are tests which currently fail, but should be fixed later.
#[cfg(not(feature = "nightly"))]
#[test]
fn todo() {
    let t = TestCases::new();
    t.compile_fail("tests/todo/*.rs");
}

/// These are tests which are intended to always fail.
#[cfg(feature = "nightly")]
#[test]
fn compile_fail() {
    let t = TestCases::new();
    t.compile_fail("tests/nightly/compile-fail/*.rs");
}
