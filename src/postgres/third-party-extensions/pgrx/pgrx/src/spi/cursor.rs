use std::ffi::CStr;
use std::marker::PhantomData;
use std::ptr::NonNull;

use crate::pg_sys;

use super::{SpiClient, SpiOkCodes, SpiResult, SpiTupleTable};

type CursorName = String;

/// An SPI Cursor from a query
///
/// Represents a Postgres cursor (internally, a portal), allowing to retrieve result rows a few
/// at a time. Moreover, a cursor can be left open within a transaction, and accessed in
/// multiple independent Spi sessions within the transaction.
///
/// A cursor can be created via [`SpiClient::open_cursor()`] from a query.
/// Cursors are automatically closed on drop, unless explicitly left open using
/// [`Self::detach_into_name()`], which returns the cursor name; cursors left open can be retrieved
/// by name (in the same transaction) via [`SpiClient::find_cursor()`].
///
/// # Important notes about memory usage
/// Result sets ([`SpiTupleTable`]s) returned by [`SpiCursor::fetch()`] will not be freed until
/// the current Spi session is complete;
/// this is a Pgrx limitation that might get lifted in the future.
///
/// In the meantime, if you're using cursors to limit memory usage, make sure to use
/// multiple separate Spi sessions, retrieving the cursor by name.
///
/// # Examples
/// ## Simple cursor
/// ```rust,no_run
/// use pgrx::prelude::*;
/// # fn foo() -> spi::Result<()> {
/// Spi::connect_mut(|client| {
///     let mut cursor = client.open_cursor("SELECT * FROM generate_series(1, 5)", &[]);
///     assert_eq!(Some(1), cursor.fetch(1)?.get_one::<i32>()?);
///     assert_eq!(Some(2), cursor.fetch(2)?.get_one::<i32>()?);
///     assert_eq!(Some(3), cursor.fetch(3)?.get_one::<i32>()?);
///     Ok::<_, pgrx::spi::Error>(())
///     // <--- all three SpiTupleTable get freed by Spi::connect at this point
/// })
/// # }
/// ```
///
/// ## Cursor by name
/// ```rust,no_run
/// use pgrx::prelude::*;
/// # fn foo() -> spi::Result<()> {
/// let cursor_name = Spi::connect_mut(|client| {
///     let mut cursor = client.open_cursor("SELECT * FROM generate_series(1, 5)", &[]);
///     assert_eq!(Ok(Some(1)), cursor.fetch(1)?.get_one::<i32>());
///     Ok::<_, spi::Error>(cursor.detach_into_name()) // <-- cursor gets dropped here
///     // <--- first SpiTupleTable gets freed by Spi::connect at this point
/// })?;
/// Spi::connect_mut(|client| {
///     let mut cursor = client.find_cursor(&cursor_name)?;
///     assert_eq!(Ok(Some(2)), cursor.fetch(1)?.get_one::<i32>());
///     drop(cursor); // <-- cursor gets dropped here
///     // ... more code ...
///     Ok(())
///     // <--- second SpiTupleTable gets freed by Spi::connect at this point
/// })
/// # }
/// ```
pub struct SpiCursor<'client> {
    pub(crate) ptr: NonNull<pg_sys::PortalData>,
    pub(crate) __marker: PhantomData<&'client SpiClient<'client>>,
}

impl SpiCursor<'_> {
    /// Fetch up to `count` rows from the cursor, moving forward
    ///
    /// If `fetch` runs off the end of the available rows, an empty [`SpiTupleTable`] is returned.
    pub fn fetch(&mut self, count: libc::c_long) -> SpiResult<SpiTupleTable> {
        // SAFETY: no concurrent access
        unsafe {
            pg_sys::SPI_tuptable = std::ptr::null_mut();
        }
        // SAFETY: SPI functions to create/find cursors fail via elog, so self.ptr is valid if we successfully set it
        unsafe { pg_sys::SPI_cursor_fetch(self.ptr.as_mut(), true, count) }
        SpiClient::prepare_tuple_table(SpiOkCodes::Fetch as i32)
    }

    /// Consume the cursor, returning its name
    ///
    /// The actual Postgres cursor is kept alive for the duration of the transaction.
    /// This allows to fetch it in a later SPI session within the same transaction
    /// using [`SpiClient::find_cursor()`]
    ///
    /// # Panics
    ///
    /// This function will panic if the cursor's name contains a null byte.
    pub fn detach_into_name(self) -> CursorName {
        // SAFETY: SPI functions to create/find cursors fail via elog, so self.ptr is valid if we successfully set it
        let cursor_ptr = unsafe { self.ptr.as_ref() };
        // Forget self, as to avoid closing the cursor in `drop`
        // No risk leaking rust memory, as Self is just a thin wrapper around a NonNull ptr
        std::mem::forget(self);
        // SAFETY: name is a null-terminated, valid string pointer from postgres
        unsafe { CStr::from_ptr(cursor_ptr.name) }
            .to_str()
            .expect("cursor name is not valid UTF8")
            .to_string()
    }
}

impl Drop for SpiCursor<'_> {
    fn drop(&mut self) {
        // SAFETY: SPI functions to create/find cursors fail via elog, so self.ptr is valid if we successfully set it
        unsafe {
            pg_sys::SPI_cursor_close(self.ptr.as_mut());
        }
    }
}
