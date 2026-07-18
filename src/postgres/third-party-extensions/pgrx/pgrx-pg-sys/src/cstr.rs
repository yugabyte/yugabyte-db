use core::ffi;

/// A trait for converting a thing into a `char *` that is allocated by Postgres' palloc
pub trait AsPgCStr {
    /// Consumes `self` and converts it into a Postgres-allocated `char *`
    fn as_pg_cstr(self) -> *mut ffi::c_char;
}

impl<'a> AsPgCStr for &'a str {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        let self_bytes = self.as_bytes();
        let pg_cstr = unsafe { crate::palloc0(self_bytes.len() + 1) as *mut u8 };
        let slice = unsafe { std::slice::from_raw_parts_mut(pg_cstr, self_bytes.len()) };
        slice.copy_from_slice(self_bytes);
        pg_cstr as *mut ffi::c_char
    }
}

impl<'a> AsPgCStr for Option<&'a str> {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        match self {
            Some(s) => s.as_pg_cstr(),
            None => std::ptr::null_mut(),
        }
    }
}

impl AsPgCStr for String {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        self.as_str().as_pg_cstr()
    }
}

impl AsPgCStr for &String {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        self.as_str().as_pg_cstr()
    }
}

impl AsPgCStr for Option<String> {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        match self {
            Some(s) => s.as_pg_cstr(),
            None => std::ptr::null_mut(),
        }
    }
}

impl AsPgCStr for Option<&String> {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        match self {
            Some(s) => s.as_pg_cstr(),
            None => std::ptr::null_mut(),
        }
    }
}

impl AsPgCStr for &Option<String> {
    fn as_pg_cstr(self) -> *mut ffi::c_char {
        match self {
            Some(s) => s.as_pg_cstr(),
            None => std::ptr::null_mut(),
        }
    }
}
