///
/// # Postgres C Macros
///
/// PGRX does not include bindings the Postgres C macros
///
/// We're reimplementing a few useful ones
///
use pgrx::pg_sys;

#[allow(non_snake_case)]
pub fn OidIsValid(objectId: pg_sys::Oid) -> bool {
    objectId != pg_sys::InvalidOid
}
