/// # Custom logging functions
///
/// We override the PGRX logging macros because we want to restrict debug
/// output to debug mode.
/// A masked user is allowed to change the value of `client_min_messages`
/// and can easily access to internal information from the debug logs.
///
#[macro_export]
macro_rules! debug1 {
    ($($e:expr),+) => {
        {
            #[cfg(debug_assertions)]
            {
                pg_sys::debug1!($($e),+)
            }
        }
    };
}
pub(crate) use debug1;

#[macro_export]
macro_rules! debug3 {
    ($($e:expr),+) => {
        {
            #[cfg(debug_assertions)]
            {
                pg_sys::debug3!($($e),+)
            }
        }
    };
}
pub(crate) use debug3;
