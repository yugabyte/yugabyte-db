use std::marker::PhantomData;
use std::ptr::NonNull;

use crate::datum::DatumWithOid;
use crate::pg_sys::{self, PgOid};
use crate::spi::{PreparedStatement, Query, Spi, SpiCursor, SpiError, SpiResult, SpiTupleTable};

use super::query::PreparableQuery;

// TODO: should `'conn` be invariant?
pub struct SpiClient<'conn> {
    __marker: PhantomData<&'conn ()>,
}

impl<'conn> SpiClient<'conn> {
    /// Connect to Postgres' SPI system.
    pub(super) fn connect() -> SpiResult<Self> {
        // SPI_connect() is documented as being able to return SPI_ERROR_CONNECT, so we have to
        // assume it could.  The truth seems to be that it never actually does.
        Spi::check_status(unsafe { pg_sys::SPI_connect() })?;
        Ok(SpiClient { __marker: PhantomData })
    }

    /// Prepares a statement that is valid for the lifetime of the client.
    pub fn prepare<Q: PreparableQuery<'conn>>(
        &self,
        query: Q,
        args: &[PgOid],
    ) -> SpiResult<PreparedStatement<'conn>> {
        query.prepare(self, args)
    }

    /// Prepares a mutating statement that is valid for the lifetime of the client.
    pub fn prepare_mut<Q: PreparableQuery<'conn>>(
        &self,
        query: Q,
        args: &[PgOid],
    ) -> SpiResult<PreparedStatement<'conn>> {
        query.prepare_mut(self, args)
    }

    /// Perform a SELECT statement.
    pub fn select<'mcx, Q: Query<'conn>>(
        &self,
        query: Q,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        query.execute(self, limit, args)
    }

    /// Perform any query (including utility statements) that modify the database in some way.
    pub fn update<'mcx, Q: Query<'conn>>(
        &mut self,
        query: Q,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        Spi::mark_mutable();
        query.execute(self, limit, args)
    }

    pub(super) fn prepare_tuple_table(
        status_code: i32,
    ) -> std::result::Result<SpiTupleTable<'conn>, SpiError> {
        Ok(SpiTupleTable {
            status_code: Spi::check_status(status_code)?,
            // SAFETY: no concurrent access
            table: unsafe { pg_sys::SPI_tuptable.as_mut() },
            // SAFETY: no concurrent access
            size: unsafe {
                if pg_sys::SPI_tuptable.is_null() {
                    pg_sys::SPI_processed as usize
                } else {
                    (*pg_sys::SPI_tuptable).numvals as usize
                }
            },
            current: -1,
        })
    }

    /// Set up a cursor that will execute the specified query.
    ///
    /// Rows may be then fetched using [`SpiCursor::fetch`].
    ///
    /// See [`SpiCursor`] docs for usage details.
    ///
    /// See [`try_open_cursor`][Self::try_open_cursor] which will return an [`SpiError`] rather than panicking.
    ///
    /// # Panics
    ///
    /// Panics if a cursor wasn't opened.
    pub fn open_cursor<'mcx, Q: Query<'conn>>(
        &self,
        query: Q,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiCursor<'conn> {
        self.try_open_cursor(query, args).unwrap()
    }

    /// Set up a cursor that will execute the specified query.
    ///
    /// Rows may be then fetched using [`SpiCursor::fetch`].
    ///
    /// See [`SpiCursor`] docs for usage details.
    pub fn try_open_cursor<'mcx, Q: Query<'conn>>(
        &self,
        query: Q,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        query.try_open_cursor(self, args)
    }

    /// Set up a cursor that will execute the specified update (mutating) query.
    ///
    /// Rows may be then fetched using [`SpiCursor::fetch`].
    ///
    /// See [`SpiCursor`] docs for usage details.
    ///
    /// See [`try_open_cursor_mut`][Self::try_open_cursor_mut] which will return an [`SpiError`] rather than panicking.
    ///
    /// # Panics
    ///
    /// Panics if a cursor wasn't opened.
    pub fn open_cursor_mut<'mcx, Q: Query<'conn>>(
        &mut self,
        query: Q,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiCursor<'conn> {
        Spi::mark_mutable();
        self.try_open_cursor_mut(query, args).unwrap()
    }

    /// Set up a cursor that will execute the specified update (mutating) query.
    ///
    /// Rows may be then fetched using [`SpiCursor::fetch`].
    ///
    /// See [`SpiCursor`] docs for usage details.
    pub fn try_open_cursor_mut<'mcx, Q: Query<'conn>>(
        &mut self,
        query: Q,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        Spi::mark_mutable();
        query.try_open_cursor(self, args)
    }

    /// Find a cursor in transaction by name.
    ///
    /// A cursor for a query can be opened using [`SpiClient::open_cursor`].
    /// Cursor are automatically closed on drop unless [`SpiCursor::detach_into_name`] is used.
    /// Returned name can be used with this method to retrieve the open cursor.
    ///
    /// See [`SpiCursor`] docs for usage details.
    pub fn find_cursor(&self, name: &str) -> SpiResult<SpiCursor<'conn>> {
        use pgrx_pg_sys::AsPgCStr;

        let ptr = NonNull::new(unsafe { pg_sys::SPI_cursor_find(name.as_pg_cstr()) })
            .ok_or(SpiError::CursorNotFound(name.to_string()))?;
        Ok(SpiCursor { ptr, __marker: PhantomData })
    }
}

impl Drop for SpiClient<'_> {
    /// When `SpiClient` is dropped, we make sure to disconnect from SPI.
    fn drop(&mut self) {
        // Best efforts to disconnect from SPI
        // SPI_finish() would only complain if we hadn't previously called SPI_connect() and
        // SpiConnection should prevent that from happening (assuming users don't go unsafe{})
        Spi::check_status(unsafe { pg_sys::SPI_finish() }).ok();
    }
}
