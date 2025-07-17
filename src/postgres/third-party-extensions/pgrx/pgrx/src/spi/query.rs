use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::ops::Deref;
use std::ptr::NonNull;

use libc::c_char;

use super::{Spi, SpiClient, SpiCursor, SpiError, SpiResult, SpiTupleTable};
use crate::{
    datum::DatumWithOid,
    pg_sys::{self, PgOid},
};

/// A generalized interface to what constitutes a query
///
/// Its primary purpose is to abstract away differences between
/// one-off statements and prepared statements, but it can potentially
/// be implemented for other types, provided they can be converted into a query.
pub trait Query<'conn>: Sized {
    /// Execute a query given a client and other arguments.
    fn execute<'mcx>(
        self,
        client: &SpiClient<'conn>,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>>;

    /// Open a cursor for the query.
    ///
    /// # Panics
    ///
    /// Panics if a cursor wasn't opened.
    #[deprecated(since = "0.12.2", note = "undefined behavior")]
    fn open_cursor<'mcx>(
        self,
        client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiCursor<'conn> {
        self.try_open_cursor(client, args).unwrap()
    }

    /// Tries to open cursor for the query.
    fn try_open_cursor<'mcx>(
        self,
        client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>>;
}

/// A trait representing a query which can be prepared.
pub trait PreparableQuery<'conn>: Query<'conn> {
    /// Prepares a query.
    fn prepare(
        self,
        client: &SpiClient<'conn>,
        args: &[PgOid],
    ) -> SpiResult<PreparedStatement<'conn>>;

    /// Prepares a query allowed to change data
    fn prepare_mut(
        self,
        client: &SpiClient<'conn>,
        args: &[PgOid],
    ) -> SpiResult<PreparedStatement<'conn>>;
}

fn execute<'conn, 'mcx>(
    cmd: &CStr,
    args: &[DatumWithOid<'mcx>],
    limit: Option<libc::c_long>,
) -> SpiResult<SpiTupleTable<'conn>> {
    // SAFETY: no concurrent access
    unsafe {
        pg_sys::SPI_tuptable = std::ptr::null_mut();
    }

    let status_code = match args.len() {
        // SAFETY: arguments are prepared above
        0 => unsafe {
            pg_sys::SPI_execute(cmd.as_ptr(), Spi::is_xact_still_immutable(), limit.unwrap_or(0))
        },
        nargs => {
            let (mut argtypes, mut datums, nulls) = args_to_datums(args);

            // SAFETY: arguments are prepared above
            unsafe {
                pg_sys::SPI_execute_with_args(
                    cmd.as_ptr(),
                    nargs as i32,
                    argtypes.as_mut_ptr(),
                    datums.as_mut_ptr(),
                    nulls.as_ptr(),
                    Spi::is_xact_still_immutable(),
                    limit.unwrap_or(0),
                )
            }
        }
    };

    SpiClient::prepare_tuple_table(status_code)
}

fn open_cursor<'conn, 'mcx>(
    cmd: &CStr,
    args: &[DatumWithOid<'mcx>],
) -> SpiResult<SpiCursor<'conn>> {
    let nargs = args.len();
    let (mut argtypes, mut datums, nulls) = args_to_datums(args);

    let ptr = unsafe {
        // SAFETY: arguments are prepared above and SPI_cursor_open_with_args will never return
        // the null pointer.  It'll raise an ERROR if something is invalid for it to create the cursor
        NonNull::new_unchecked(pg_sys::SPI_cursor_open_with_args(
            std::ptr::null_mut(), // let postgres assign a name
            cmd.as_ptr(),
            nargs as i32,
            argtypes.as_mut_ptr(),
            datums.as_mut_ptr(),
            nulls.as_ptr(),
            Spi::is_xact_still_immutable(),
            0,
        ))
    };

    Ok(SpiCursor { ptr, __marker: PhantomData })
}

fn args_to_datums<'mcx>(
    args: &[DatumWithOid<'mcx>],
) -> (Vec<pg_sys::Oid>, Vec<pg_sys::Datum>, Vec<c_char>) {
    let mut argtypes = Vec::with_capacity(args.len());
    let mut datums = Vec::with_capacity(args.len());
    let mut nulls = Vec::with_capacity(args.len());

    for arg in args {
        let (datum, null) = prepare_datum(arg);

        argtypes.push(arg.oid());
        datums.push(datum);
        nulls.push(null);
    }

    (argtypes, datums, nulls)
}

fn prepare_datum<'mcx>(datum: &DatumWithOid<'mcx>) -> (pg_sys::Datum, std::os::raw::c_char) {
    match datum.datum() {
        Some(datum) => (datum.sans_lifetime(), ' ' as std::os::raw::c_char),
        None => (pg_sys::Datum::from(0usize), 'n' as std::os::raw::c_char),
    }
}

fn prepare<'conn>(
    cmd: &CStr,
    args: &[PgOid],
    mutating: bool,
) -> SpiResult<PreparedStatement<'conn>> {
    // SAFETY: all arguments are prepared above
    let plan = unsafe {
        pg_sys::SPI_prepare(
            cmd.as_ptr(),
            args.len() as i32,
            args.iter().map(|arg| arg.value()).collect::<Vec<_>>().as_mut_ptr(),
        )
    };
    Ok(PreparedStatement {
        plan: NonNull::new(plan).ok_or_else(|| {
            Spi::check_status(unsafe {
                // SAFETY: no concurrent usage
                pg_sys::SPI_result
            })
            .err()
            .unwrap()
        })?,
        __marker: PhantomData,
        mutating,
    })
}

macro_rules! impl_prepared_query {
    ($t:ident, $s:ident) => {
        impl<'conn> Query<'conn> for &$t {
            #[inline]
            fn execute(
                self,
                _client: &SpiClient<'conn>,
                limit: Option<libc::c_long>,
                args: &[DatumWithOid],
            ) -> SpiResult<SpiTupleTable<'conn>> {
                execute($s(self).as_ref(), args, limit)
            }

            #[inline]
            fn try_open_cursor(
                self,
                _client: &SpiClient<'conn>,
                args: &[DatumWithOid],
            ) -> SpiResult<SpiCursor<'conn>> {
                open_cursor($s(self).as_ref(), args)
            }
        }

        impl<'conn> PreparableQuery<'conn> for &$t {
            fn prepare(
                self,
                _client: &SpiClient<'conn>,
                args: &[PgOid],
            ) -> SpiResult<PreparedStatement<'conn>> {
                prepare($s(self).as_ref(), args, false)
            }

            fn prepare_mut(
                self,
                _client: &SpiClient<'conn>,
                args: &[PgOid],
            ) -> SpiResult<PreparedStatement<'conn>> {
                prepare($s(self).as_ref(), args, true)
            }
        }
    };
}

#[inline]
fn pass_as_is<T>(s: T) -> T {
    s
}

#[inline]
fn pass_with_conv<T: AsRef<str>>(s: T) -> CString {
    CString::new(s.as_ref()).expect("query contained a null byte")
}

impl_prepared_query!(CStr, pass_as_is);
impl_prepared_query!(CString, pass_as_is);
impl_prepared_query!(String, pass_with_conv);
impl_prepared_query!(str, pass_with_conv);

/// Client lifetime-bound prepared statement
pub struct PreparedStatement<'conn> {
    pub(super) plan: NonNull<pg_sys::_SPI_plan>,
    pub(super) __marker: PhantomData<&'conn ()>,
    pub(super) mutating: bool,
}

/// Static lifetime-bound prepared statement
pub struct OwnedPreparedStatement(PreparedStatement<'static>);

impl Deref for OwnedPreparedStatement {
    type Target = PreparedStatement<'static>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for OwnedPreparedStatement {
    fn drop(&mut self) {
        unsafe {
            pg_sys::SPI_freeplan(self.0.plan.as_ptr());
        }
    }
}

impl<'conn> Query<'conn> for &OwnedPreparedStatement {
    fn execute<'mcx>(
        self,
        client: &SpiClient<'conn>,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        (&self.0).execute(client, limit, args)
    }

    fn try_open_cursor<'mcx>(
        self,
        client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        (&self.0).try_open_cursor(client, args)
    }
}

impl<'conn> Query<'conn> for OwnedPreparedStatement {
    fn execute<'mcx>(
        self,
        client: &SpiClient<'conn>,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        (&self.0).execute(client, limit, args)
    }

    fn try_open_cursor<'mcx>(
        self,
        client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        (&self.0).try_open_cursor(client, args)
    }
}

impl<'conn> PreparedStatement<'conn> {
    /// Converts prepared statement into an owned prepared statement
    ///
    /// These statements have static lifetime and are freed only when dropped
    pub fn keep(self) -> OwnedPreparedStatement {
        // SAFETY: self.plan is initialized in `SpiClient::prepare` and `PreparedStatement`
        // is consumed. If it wasn't consumed, a subsequent call to `keep` would trigger
        // an SPI_ERROR_ARGUMENT as per `SPI_keepplan` implementation.
        unsafe {
            pg_sys::SPI_keepplan(self.plan.as_ptr());
        }
        OwnedPreparedStatement(PreparedStatement {
            __marker: PhantomData,
            plan: self.plan,
            mutating: self.mutating,
        })
    }

    fn args_to_datums<'mcx>(
        &self,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<(Vec<pg_sys::Datum>, Vec<std::os::raw::c_char>)> {
        let actual = args.len();
        let expected = unsafe { pg_sys::SPI_getargcount(self.plan.as_ptr()) } as usize;

        if expected == actual {
            Ok(args.iter().map(prepare_datum).unzip())
        } else {
            Err(SpiError::PreparedStatementArgumentMismatch { expected, got: actual })
        }
    }
}

impl<'conn: 'stmt, 'stmt> Query<'conn> for &'stmt PreparedStatement<'conn> {
    fn execute<'mcx>(
        self,
        _client: &SpiClient<'conn>,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        // SAFETY: no concurrent access
        unsafe {
            pg_sys::SPI_tuptable = std::ptr::null_mut();
        }

        let (mut datums, mut nulls) = self.args_to_datums(args)?;

        // SAFETY: all arguments are prepared above
        let status_code = unsafe {
            pg_sys::SPI_execute_plan(
                self.plan.as_ptr(),
                datums.as_mut_ptr(),
                nulls.as_mut_ptr(),
                !self.mutating && Spi::is_xact_still_immutable(),
                limit.unwrap_or(0),
            )
        };

        SpiClient::prepare_tuple_table(status_code)
    }

    fn try_open_cursor<'mcx>(
        self,
        _client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        let (mut datums, nulls) = self.args_to_datums(args)?;

        // SAFETY: arguments are prepared above and SPI_cursor_open will never return the null
        // pointer.  It'll raise an ERROR if something is invalid for it to create the cursor
        let ptr = unsafe {
            NonNull::new_unchecked(pg_sys::SPI_cursor_open(
                std::ptr::null_mut(), // let postgres assign a name
                self.plan.as_ptr(),
                datums.as_mut_ptr(),
                nulls.as_ptr(),
                !self.mutating && Spi::is_xact_still_immutable(),
            ))
        };
        Ok(SpiCursor { ptr, __marker: PhantomData })
    }
}

impl<'conn> Query<'conn> for PreparedStatement<'conn> {
    fn execute<'mcx>(
        self,
        client: &SpiClient<'conn>,
        limit: Option<libc::c_long>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiTupleTable<'conn>> {
        (&self).execute(client, limit, args)
    }

    fn try_open_cursor<'mcx>(
        self,
        client: &SpiClient<'conn>,
        args: &[DatumWithOid<'mcx>],
    ) -> SpiResult<SpiCursor<'conn>> {
        (&self).try_open_cursor(client, args)
    }
}
