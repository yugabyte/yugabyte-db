use std::ffi::{CStr, CString};
use std::fmt::Debug;
use std::marker::PhantomData;
use std::ops::Index;
use std::ptr::NonNull;

use crate::memcxt::PgMemoryContexts;
use crate::pg_sys::panic::ErrorReportable;
use crate::prelude::*;

use super::{SpiError, SpiErrorCodes, SpiOkCodes, SpiResult};

#[derive(Debug)]
pub struct SpiTupleTable<'conn> {
    #[allow(dead_code)]
    pub(super) status_code: SpiOkCodes,
    pub(super) table: Option<&'conn mut pg_sys::SPITupleTable>,
    pub(super) size: usize,
    pub(super) current: isize,
}

impl<'conn> SpiTupleTable<'conn> {
    /// `SpiTupleTable`s are positioned before the start, for iteration purposes.
    ///
    /// This method moves the position to the first row.  If there are no rows, this
    /// method will silently return.
    pub fn first(mut self) -> Self {
        self.current = 0;
        self
    }

    /// Restore the state of iteration back to before the start.
    ///
    /// This is useful to iterate the table multiple times
    pub fn rewind(mut self) -> Self {
        self.current = -1;
        self
    }

    /// How many rows were processed?
    pub fn len(&self) -> usize {
        self.size
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn get_one<A: FromDatum + IntoDatum>(&self) -> SpiResult<Option<A>> {
        self.get(1)
    }

    pub fn get_two<A: FromDatum + IntoDatum, B: FromDatum + IntoDatum>(
        &self,
    ) -> SpiResult<(Option<A>, Option<B>)> {
        let a = self.get::<A>(1)?;
        let b = self.get::<B>(2)?;
        Ok((a, b))
    }

    pub fn get_three<
        A: FromDatum + IntoDatum,
        B: FromDatum + IntoDatum,
        C: FromDatum + IntoDatum,
    >(
        &self,
    ) -> SpiResult<(Option<A>, Option<B>, Option<C>)> {
        let a = self.get::<A>(1)?;
        let b = self.get::<B>(2)?;
        let c = self.get::<C>(3)?;
        Ok((a, b, c))
    }

    #[inline(always)]
    fn get_spi_tuptable(
        &self,
    ) -> SpiResult<(*mut pg_sys::SPITupleTable, *mut pg_sys::TupleDescData)> {
        let table = self.table.as_deref().ok_or(SpiError::NoTupleTable)?;
        // SAFETY:  we just assured that `table` is not null
        Ok((table as *const _ as *mut _, table.tupdesc))
    }

    pub fn get_heap_tuple(&self) -> SpiResult<Option<SpiHeapTupleData<'conn>>> {
        if self.size == 0 || self.table.is_none() {
            // a query like "SELECT 1 LIMIT 0" is a valid "select"-style query that will not produce
            // a SPI_tuptable.  So are utility queries such as "CREATE INDEX" or "VACUUM".  We might
            // think that in the latter cases we'd want to produce an error here, but there's no
            // way to distinguish from the former.  As such, we take a gentle approach and
            // processed with "no, we don't have one, but it's okay"
            Ok(None)
        } else if self.current < 0 || self.current as usize >= self.size {
            Err(SpiError::InvalidPosition)
        } else {
            let (table, tupdesc) = self.get_spi_tuptable()?;
            unsafe {
                let heap_tuple =
                    std::slice::from_raw_parts((*table).vals, self.size)[self.current as usize];

                // SAFETY:  we know heap_tuple is valid because we just made it
                SpiHeapTupleData::new(tupdesc, heap_tuple)
            }
        }
    }

    /// Get a typed value by its ordinal position.
    ///
    /// The ordinal position is 1-based.
    ///
    /// # Errors
    ///
    /// If the specified ordinal is out of bounds an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    ///
    /// # Panics
    ///
    /// This function will panic there is no parent MemoryContext.  This is an incredibly unlikely
    /// situation.
    pub fn get<T: IntoDatum + FromDatum>(&self, ordinal: usize) -> SpiResult<Option<T>> {
        let (_, tupdesc) = self.get_spi_tuptable()?;
        let datum = self.get_datum_by_ordinal(ordinal)?;
        let is_null = datum.is_none();
        let datum = datum.unwrap_or_else(|| pg_sys::Datum::from(0));

        unsafe {
            // SAFETY:  we know the constraints around `datum` and `is_null` match because we
            // just got them from the underlying heap tuple
            Ok(T::try_from_datum_in_memory_context(
                PgMemoryContexts::CurrentMemoryContext
                    .parent()
                    .expect("parent memory context is absent"),
                datum,
                is_null,
                // SAFETY:  we know `self.tupdesc.is_some()` because an Ok return from
                // `self.get_datum_by_ordinal()` above already decided that for us
                pg_sys::SPI_gettypeid(tupdesc, ordinal as _),
            )?)
        }
    }

    /// Get a typed value by its name.
    ///
    /// # Errors
    ///
    /// If the specified name is invalid an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    pub fn get_by_name<T: IntoDatum + FromDatum, S: AsRef<str>>(
        &self,
        name: S,
    ) -> SpiResult<Option<T>> {
        self.get(self.column_ordinal(name)?)
    }

    /// Get a raw Datum from this HeapTuple by its ordinal position.
    ///
    /// The ordinal position is 1-based.
    ///
    /// # Errors
    ///
    /// If the specified ordinal is out of bounds an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    pub fn get_datum_by_ordinal(&self, ordinal: usize) -> SpiResult<Option<pg_sys::Datum>> {
        self.check_ordinal_bounds(ordinal)?;

        let (table, tupdesc) = self.get_spi_tuptable()?;
        if self.current < 0 || self.current as usize >= self.size {
            return Err(SpiError::InvalidPosition);
        }
        unsafe {
            let heap_tuple =
                std::slice::from_raw_parts((*table).vals, self.size)[self.current as usize];
            let mut is_null = false;
            let datum = pg_sys::SPI_getbinval(heap_tuple, tupdesc, ordinal as _, &mut is_null);

            if is_null {
                Ok(None)
            } else {
                Ok(Some(datum))
            }
        }
    }

    /// Get a raw Datum from this HeapTuple by its column name.
    ///
    /// # Errors
    ///
    /// If the specified name is invalid an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    pub fn get_datum_by_name<S: AsRef<str>>(&self, name: S) -> SpiResult<Option<pg_sys::Datum>> {
        self.get_datum_by_ordinal(self.column_ordinal(name)?)
    }

    /// Returns the number of columns
    pub fn columns(&self) -> SpiResult<usize> {
        let (_, tupdesc) = self.get_spi_tuptable()?;
        // SAFETY:  we just got a valid tupdesc
        Ok(unsafe { (*tupdesc).natts as _ })
    }

    /// is the specified ordinal valid for the underlying tuple descriptor?
    #[inline]
    fn check_ordinal_bounds(&self, ordinal: usize) -> SpiResult<()> {
        if ordinal < 1 || ordinal > self.columns()? {
            Err(SpiError::SpiError(SpiErrorCodes::NoAttribute))
        } else {
            Ok(())
        }
    }

    /// Returns column type OID
    ///
    /// The ordinal position is 1-based
    pub fn column_type_oid(&self, ordinal: usize) -> SpiResult<PgOid> {
        self.check_ordinal_bounds(ordinal)?;

        let (_, tupdesc) = self.get_spi_tuptable()?;
        unsafe {
            // SAFETY:  we just got a valid tupdesc
            let oid = pg_sys::SPI_gettypeid(tupdesc, ordinal as i32);
            Ok(PgOid::from(oid))
        }
    }

    /// Returns column name of the 1-based `ordinal` position
    ///
    /// # Errors
    ///
    /// Returns [`Error::SpiError(SpiError::NoAttribute)`] if the specified ordinal value is out of bounds
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    ///
    /// # Panics
    ///
    /// This function will panic if the column name at the specified ordinal position is not also
    /// a valid UTF8 string.
    pub fn column_name(&self, ordinal: usize) -> SpiResult<String> {
        self.check_ordinal_bounds(ordinal)?;
        let (_, tupdesc) = self.get_spi_tuptable()?;
        unsafe {
            // SAFETY:  we just got a valid tupdesc and we know ordinal is in bounds
            let name = pg_sys::SPI_fname(tupdesc, ordinal as i32);

            // SAFETY:  SPI_fname will have given us a properly allocated char* since we know
            // the specified ordinal is in bounds
            let str =
                CStr::from_ptr(name).to_str().expect("column name is not value UTF8").to_string();

            // SAFETY: we just asked Postgres to allocate name for us
            pg_sys::pfree(name as *mut _);
            Ok(str)
        }
    }

    /// Returns the ordinal (1-based position) of the specified column name
    ///
    /// # Errors
    ///
    /// Returns [`Error::SpiError(SpiError::NoAttribute)`] if the specified column name isn't found
    /// If we have no backing tuple table an [`SpiError::NoTupleTable`] is returned
    ///
    /// # Panics
    ///
    /// This function will panic if somehow the specified name contains a null byte.
    pub fn column_ordinal<S: AsRef<str>>(&self, name: S) -> SpiResult<usize> {
        let (_, tupdesc) = self.get_spi_tuptable()?;
        unsafe {
            let name_cstr = CString::new(name.as_ref()).expect("name contained a null byte");
            let fnumber = pg_sys::SPI_fnumber(tupdesc, name_cstr.as_ptr());

            if fnumber == pg_sys::SPI_ERROR_NOATTRIBUTE {
                Err(SpiError::SpiError(SpiErrorCodes::NoAttribute))
            } else {
                Ok(fnumber as usize)
            }
        }
    }
}

impl<'conn> Iterator for SpiTupleTable<'conn> {
    type Item = SpiHeapTupleData<'conn>;

    /// # Panics
    ///
    /// This method will panic if for some reason the underlying heap tuple cannot be retrieved
    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        self.current += 1;
        if self.current >= self.size as isize {
            None
        } else {
            assert!(self.current >= 0);
            self.get_heap_tuple().unwrap_or_report()
        }
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, Some(self.size))
    }
}

/// Represents a single `pg_sys::Datum` inside a `SpiHeapTupleData`
pub struct SpiHeapTupleDataEntry<'conn> {
    datum: Option<pg_sys::Datum>,
    type_oid: pg_sys::Oid,
    __marker: PhantomData<&'conn ()>,
}

/// Represents the set of `pg_sys::Datum`s in a `pg_sys::HeapTuple`
pub struct SpiHeapTupleData<'conn> {
    tupdesc: NonNull<pg_sys::TupleDescData>,
    // offset by 1!
    entries: Vec<SpiHeapTupleDataEntry<'conn>>,
}

impl<'conn> SpiHeapTupleData<'conn> {
    /// Create a new `SpiHeapTupleData` from its constituent parts
    ///
    /// # Safety
    ///
    /// This is unsafe as it cannot ensure that the provided `tupdesc` and `htup` arguments
    /// are valid, palloc'd pointers.
    pub unsafe fn new(
        tupdesc: pg_sys::TupleDesc,
        htup: *mut pg_sys::HeapTupleData,
    ) -> SpiResult<Option<Self>> {
        let tupdesc = NonNull::new(tupdesc).ok_or(SpiError::NoTupleTable)?;
        let mut data = SpiHeapTupleData { tupdesc, entries: Vec::new() };
        let tupdesc = tupdesc.as_ptr();

        unsafe {
            // SAFETY:  we know tupdesc is not null
            let natts = (*tupdesc).natts;
            data.entries.reserve(usize::try_from(natts).unwrap());
            for i in 1..=natts {
                let mut is_null = false;
                let datum = pg_sys::SPI_getbinval(htup, tupdesc as _, i, &mut is_null);
                data.entries.push(SpiHeapTupleDataEntry {
                    datum: if is_null { None } else { Some(datum) },
                    type_oid: pg_sys::SPI_gettypeid(tupdesc as _, i),
                    __marker: PhantomData,
                });
            }
        }

        Ok(Some(data))
    }

    /// Get a typed value from this HeapTuple by its ordinal position.
    ///
    /// The ordinal position is 1-based
    ///
    /// # Errors
    ///
    /// Returns an [`SpiError::DatumError`] if the desired Rust type is incompatible
    /// with the underlying Datum
    pub fn get<T: IntoDatum + FromDatum>(&self, ordinal: usize) -> SpiResult<Option<T>> {
        self.get_datum_by_ordinal(ordinal).map(|entry| entry.value())?
    }

    /// Get a typed value from this HeapTuple by its name in the resultset.
    ///
    /// # Errors
    ///
    /// Returns an [`SpiError::DatumError`] if the desired Rust type is incompatible
    /// with the underlying Datum
    pub fn get_by_name<T: IntoDatum + FromDatum, S: AsRef<str>>(
        &self,
        name: S,
    ) -> SpiResult<Option<T>> {
        self.get_datum_by_name(name.as_ref()).map(|entry| entry.value())?
    }

    /// Get a raw Datum from this HeapTuple by its ordinal position.
    ///
    /// The ordinal position is 1-based.
    ///
    /// # Errors
    ///
    /// If the specified ordinal is out of bounds an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    pub fn get_datum_by_ordinal(&self, ordinal: usize) -> SpiResult<&SpiHeapTupleDataEntry<'conn>> {
        // Wrapping because `self.entries.get(...)` will bounds check.
        let index = ordinal.wrapping_sub(1);
        self.entries.get(index).ok_or(SpiError::SpiError(SpiErrorCodes::NoAttribute))
    }

    /// Get a raw Datum from this HeapTuple by its field name.
    ///
    /// # Errors
    ///
    /// If the specified name isn't valid an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    ///
    /// # Panics
    ///
    /// This function will panic if somehow the specified name contains a null byte.
    pub fn get_datum_by_name<S: AsRef<str>>(
        &self,
        name: S,
    ) -> SpiResult<&SpiHeapTupleDataEntry<'conn>> {
        unsafe {
            let name_cstr = CString::new(name.as_ref()).expect("name contained a null byte");
            let fnumber = pg_sys::SPI_fnumber(self.tupdesc.as_ptr(), name_cstr.as_ptr());

            if fnumber == pg_sys::SPI_ERROR_NOATTRIBUTE {
                Err(SpiError::SpiError(SpiErrorCodes::NoAttribute))
            } else {
                self.get_datum_by_ordinal(fnumber as usize)
            }
        }
    }

    /// Set a datum value for the specified ordinal position
    ///
    /// # Errors
    ///
    /// If the specified ordinal is out of bounds a [`SpiErrorCodes::NoAttribute`] is returned
    pub fn set_by_ordinal<T: IntoDatum>(&mut self, ordinal: usize, datum: T) -> SpiResult<()> {
        self.check_ordinal_bounds(ordinal)?;
        self.entries[ordinal - 1] = SpiHeapTupleDataEntry {
            datum: datum.into_datum(),
            type_oid: T::type_oid(),
            __marker: PhantomData,
        };
        Ok(())
    }

    /// Set a datum value for the specified field name
    ///
    /// # Errors
    ///
    /// If the specified name isn't valid an [`SpiError::SpiError(SpiError::NoAttribute)`] is returned
    ///
    /// # Panics
    ///
    /// This function will panic if somehow the specified name contains a null byte.
    pub fn set_by_name<T: IntoDatum>(&mut self, name: &str, datum: T) -> SpiResult<()> {
        unsafe {
            let name_cstr = CString::new(name).expect("name contained a null byte");
            let fnumber = pg_sys::SPI_fnumber(self.tupdesc.as_ptr(), name_cstr.as_ptr());
            if fnumber == pg_sys::SPI_ERROR_NOATTRIBUTE {
                Err(SpiError::SpiError(SpiErrorCodes::NoAttribute))
            } else {
                self.set_by_ordinal(fnumber as usize, datum)
            }
        }
    }

    #[inline]
    pub fn columns(&self) -> usize {
        unsafe {
            // SAFETY: we know self.tupdesc is a valid, non-null pointer because we own it
            (*self.tupdesc.as_ptr()).natts as usize
        }
    }

    /// is the specified ordinal valid for the underlying tuple descriptor?
    #[inline]
    fn check_ordinal_bounds(&self, ordinal: usize) -> SpiResult<()> {
        if ordinal < 1 || ordinal > self.columns() {
            Err(SpiError::SpiError(SpiErrorCodes::NoAttribute))
        } else {
            Ok(())
        }
    }
}

impl<'conn> SpiHeapTupleDataEntry<'conn> {
    pub fn value<T: IntoDatum + FromDatum>(&self) -> SpiResult<Option<T>> {
        match self.datum.as_ref() {
            Some(datum) => unsafe {
                T::try_from_datum_in_memory_context(
                    PgMemoryContexts::CurrentMemoryContext
                        .parent()
                        .expect("parent memory context is absent"),
                    *datum,
                    false,
                    self.type_oid,
                )
                .map_err(SpiError::DatumError)
            },
            None => Ok(None),
        }
    }

    pub fn oid(&self) -> pg_sys::Oid {
        self.type_oid
    }
}

/// Provide ordinal indexing into a `SpiHeapTupleData`.
///
/// If the index is out of bounds, it will panic
impl<'conn> Index<usize> for SpiHeapTupleData<'conn> {
    type Output = SpiHeapTupleDataEntry<'conn>;

    fn index(&self, index: usize) -> &Self::Output {
        self.get_datum_by_ordinal(index).expect("invalid ordinal value")
    }
}

/// Provide named indexing into a `SpiHeapTupleData`.
///
/// If the field name doesn't exist, it will panic
impl<'conn> Index<&str> for SpiHeapTupleData<'conn> {
    type Output = SpiHeapTupleDataEntry<'conn>;

    fn index(&self, index: &str) -> &Self::Output {
        self.get_datum_by_name(index).expect("invalid field name")
    }
}
