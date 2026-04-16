//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::list::List;
use crate::memcx::MemCx;
use crate::{pg_sys, FromDatum, IntoDatum};
use std::ptr::NonNull;

/// Provides a safe wrapper around a Postgres "SysCache" entry from `pg_catalog.pg_proc`.
pub struct PgProc {
    inner: NonNull<pg_sys::HeapTupleData>,
    oid: pg_sys::Oid,
}

#[non_exhaustive]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ProArgMode {
    In,
    Out,
    InOut,
    Variadic,
    Table,
}

impl From<i8> for ProArgMode {
    fn from(value: i8) -> Self {
        match value as u8 {
            b'i' => ProArgMode::In,
            b'o' => ProArgMode::Out,
            b'b' => ProArgMode::InOut,
            b'v' => ProArgMode::Variadic,
            b't' => ProArgMode::Table,

            // there's just no ability to move forward if given a value that we don't know about
            _ => panic!("unrecognized `ProArgMode`: `{}`", value as u8 as char),
        }
    }
}

#[non_exhaustive]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ProKind {
    Function,
    Procedure,
    Aggregate,
    Window,
}

impl From<i8> for ProKind {
    fn from(value: i8) -> Self {
        match value as u8 {
            b'f' => ProKind::Function,
            b'p' => ProKind::Procedure,
            b'a' => ProKind::Aggregate,
            b'w' => ProKind::Window,

            // there's just no ability to move forward if given a value that we don't know about
            _ => panic!("unrecognized `ProKind`: `{}`", value as u8 as char),
        }
    }
}

#[non_exhaustive]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ProVolatile {
    Immutable,
    Stable,
    Volatile,
}

impl From<i8> for ProVolatile {
    fn from(value: i8) -> Self {
        match value as u8 {
            b'i' => ProVolatile::Immutable,
            b's' => ProVolatile::Stable,
            b'v' => ProVolatile::Volatile,

            // there's just no ability to move forward if given a value that we don't know about
            _ => panic!("unrecognized `ProVolatile`: `{}`", value as u8 as char),
        }
    }
}

#[non_exhaustive]
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ProParallel {
    Safe,
    Restricted,
    Unsafe,
}

impl From<i8> for ProParallel {
    fn from(value: i8) -> Self {
        match value as u8 {
            b's' => ProParallel::Safe,
            b'r' => ProParallel::Restricted,
            b'u' => ProParallel::Unsafe,

            // there's just no ability to move forward if given a value that we don't know about
            _ => panic!("unrecognized `ProParallel`: `{}`", value as u8 as char),
        }
    }
}

impl Drop for PgProc {
    fn drop(&mut self) {
        // SAFETY: We have a valid pointer and this just decrements the reference count.
        // This will generally get resolved by the end of the transaction anyways,
        // but Postgres strongly recommends you do not do that.
        unsafe { pg_sys::ReleaseSysCache(self.inner.as_ptr()) }
    }
}

impl PgProc {
    /// Construct a new [`PgProc`] from a known function [`pg_sys::Oid`].  If the specified oid is not
    /// a function, we return [`None`].
    pub fn new(pg_proc_oid: pg_sys::Oid) -> Option<PgProc> {
        unsafe {
            // SAFETY:  SearchSysCache1 will give us a valid HeapTuple or it'll return null.
            // Either way, using NonNull::new()? will make the right decision for us
            let entry = pg_sys::SearchSysCache1(
                pg_sys::SysCacheIdentifier::PROCOID as _,
                pg_proc_oid.into_datum()?,
            );
            let inner = NonNull::new(entry)?;
            Some(PgProc { inner, oid: pg_proc_oid })
        }
    }

    /// Oid of the function
    pub fn oid(&self) -> pg_sys::Oid {
        self.oid
    }

    /// Owner of the function
    pub fn proowner(&self) -> pg_sys::Oid {
        // won't panic because `proowner` has a NOT NULL constraint
        self.get_attr(pg_sys::Anum_pg_proc_proowner).unwrap()
    }

    /// Estimated execution cost (in units of cpu_operator_cost); if [`proretset()`][PgProc::proretset],
    /// this is cost per row returned
    pub fn procost(&self) -> f32 {
        // won't panic because `procost` has a NOT NULL constraint
        self.get_attr(pg_sys::Anum_pg_proc_procost).unwrap()
    }

    /// Estimated number of result rows (zero if not [`proretset()`][PgProc::proretset])
    pub fn prorows(&self) -> f32 {
        // won't panic because `prorows` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prorows).unwrap()
    }

    /// Data type of the variadic array parameter's elements, or [`None`] if the function does not have a variadic parameter
    pub fn provariadic(&self) -> Option<pg_sys::Oid> {
        let oid = self.get_attr(pg_sys::Anum_pg_proc_provariadic).unwrap();
        if oid == pg_sys::InvalidOid {
            None
        } else {
            Some(oid)
        }
    }

    /// Planner support function for this function (see Section 38.11), or zero if none
    pub fn prosupport(&self) -> pg_sys::Oid {
        // won't panic because `prosupport` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prosupport).unwrap()
    }

    /// The kind of function
    pub fn prokind(&self) -> ProKind {
        // won't panic because `prokind` has a NOT NULL constraint, so `.unwrap()` wont panic
        ProKind::from(self.get_attr::<i8>(pg_sys::Anum_pg_proc_prokind).unwrap())
    }

    /// Returns true if the function is a security definer (i.e., a “setuid” function)
    pub fn prosecdef(&self) -> bool {
        // won't panic because `prosecdef` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prosecdef).unwrap()
    }

    /// The function has no side effects. No information about the arguments is conveyed except via
    /// the return value. Any function that might throw an error depending on the values of its
    /// arguments is not leak-proof.
    pub fn proleakproof(&self) -> bool {
        // won't panic because `proleakproof` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_proleakproof).unwrap()
    }

    /// Implementation language or call interface of this function
    pub fn prolang(&self) -> pg_sys::Oid {
        // won't panic because `prolang` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prolang).unwrap()
    }

    /// This tells the function handler how to invoke the function. It might be the actual source
    /// code of the function for interpreted languages, a link symbol, a file name, or just about
    /// anything else, depending on the implementation language/call convention.
    pub fn prosrc(&self) -> String {
        // won't panic because `prosrc` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prosrc).unwrap()
    }

    /// Additional information about how to invoke the function. Again, the interpretation is
    /// language-specific.
    pub fn probin(&self) -> Option<String> {
        self.get_attr(pg_sys::Anum_pg_proc_probin)
    }

    /// Function's local settings for run-time configuration variables
    pub fn proconfig(&self) -> Option<Vec<String>> {
        self.get_attr(pg_sys::Anum_pg_proc_proconfig)
    }

    /// From <https://www.postgresql.org/docs/current/catalog-pg-proc.html>:
    /// > An array of the modes of the function arguments, encoded as i for IN arguments, o for OUT
    /// > arguments, b for INOUT arguments, v for VARIADIC arguments, t for TABLE arguments. If all
    /// > the arguments are IN arguments, this field will be null. Note that subscripts correspond to
    /// > positions of proallargtypes not proargtypes.
    ///
    /// In our case, if all the arguments are `IN` arguments, the returned Vec will have the
    /// corresponding `ProArgModes::In` value in each element.
    pub fn proargmodes(&self) -> Vec<ProArgMode> {
        self.get_attr::<Vec<i8>>(pg_sys::Anum_pg_proc_proargmodes)
            .unwrap_or_else(|| vec!['i' as i8; self.proargnames().len()])
            .into_iter()
            .map(ProArgMode::from)
            .collect::<Vec<_>>()
    }

    /// Number of input arguments
    pub fn pronargs(&self) -> usize {
        // won't panic because `pronargs` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr::<i16>(pg_sys::Anum_pg_proc_pronargs).unwrap() as usize
    }

    /// Number of arguments that have defaults
    pub fn pronargdefaults(&self) -> usize {
        // won't panic because `pronargdefaults` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr::<i16>(pg_sys::Anum_pg_proc_pronargdefaults).unwrap() as usize
    }

    /// An array of the names of the function arguments. Arguments without a name are set to empty
    /// strings in the array. If none of the arguments have a name, this field will be null. Note
    /// that subscripts correspond to positions of proallargtypes not proargtypes.
    pub fn proargnames(&self) -> Vec<Option<String>> {
        self.get_attr::<Vec<Option<String>>>(pg_sys::Anum_pg_proc_proargnames)
            .unwrap_or_else(|| vec![None; self.pronargs()])
    }

    /// An array of the data types of the function arguments. This includes only input arguments
    /// (including INOUT and VARIADIC arguments), and thus represents the call signature of the
    /// function.
    pub fn proargtypes(&self) -> Vec<pg_sys::Oid> {
        self.get_attr(pg_sys::Anum_pg_proc_proargtypes).unwrap_or_default()
    }

    /// An array of the data types of the function arguments. This includes all arguments (including
    /// OUT and INOUT arguments); however, if all the arguments are IN arguments, this field will be
    /// null. Note that subscripting is 1-based, whereas for historical reasons proargtypes is
    /// subscripted from 0.
    pub fn proallargtypes(&self) -> Vec<pg_sys::Oid> {
        self.get_attr(pg_sys::Anum_pg_proc_proallargtypes).unwrap_or_else(|| self.proargtypes())
    }

    /// Data type of the return value
    pub fn prorettype(&self) -> pg_sys::Oid {
        // won't panic because `prorettype` has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_prorettype).unwrap()
    }

    /// Function returns null if any call argument is null. In that case the function won't actually
    /// be called at all. Functions that are not “strict” must be prepared to handle null inputs.
    pub fn proisstrict(&self) -> bool {
        // 'proisstrict' has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_proisstrict).unwrap()
    }

    /// provolatile tells whether the function's result depends only on its input arguments, or is
    /// affected by outside factors. It is i for “immutable” functions, which always deliver the
    /// same result for the same inputs. It is s for “stable” functions, whose results (for fixed
    /// inputs) do not change within a scan. It is v for “volatile” functions, whose results might
    /// change at any time. (Use v also for functions with side-effects, so that calls to them
    /// cannot get optimized away.)
    pub fn provolatile(&self) -> ProVolatile {
        // 'provolatile' has a NOT NULL constraint, so `.unwrap()` wont panic
        ProVolatile::from(self.get_attr::<i8>(pg_sys::Anum_pg_proc_provolatile).unwrap())
    }

    /// proparallel tells whether the function can be safely run in parallel mode. It is s for
    /// functions which are safe to run in parallel mode without restriction. It is r for functions
    /// which can be run in parallel mode, but their execution is restricted to the parallel group
    /// leader; parallel worker processes cannot invoke these functions. It is u for functions which
    /// are unsafe in parallel mode; the presence of such a function forces a serial execution plan.
    pub fn proparallel(&self) -> ProParallel {
        // 'proparallel' has a NOT NULL constraint, so `.unwrap()` wont panic
        ProParallel::from(self.get_attr::<i8>(pg_sys::Anum_pg_proc_proparallel).unwrap())
    }

    /// Function returns a set (i.e., multiple values of the specified data type)
    pub fn proretset(&self) -> bool {
        // 'proretset' has a NOT NULL constraint, so `.unwrap()` wont panic
        self.get_attr(pg_sys::Anum_pg_proc_proretset).unwrap()
    }

    /// Expression trees for default values. This is a [`List`] with `pronargdefaults` elements,
    /// corresponding to the last N input arguments (i.e., the last N proargtypes positions).
    ///
    /// If none of the arguments have defaults, this function returns [`Option::None`].
    pub fn proargdefaults<'cx>(
        &self,
        mcx: &'cx MemCx<'_>,
    ) -> Option<List<'cx, *mut std::ffi::c_void>> {
        unsafe {
            use pgrx_pg_sys::AsPgCStr;

            let mut is_null = false;
            let proargdefaults = pg_sys::SysCacheGetAttr(
                pg_sys::SysCacheIdentifier::PROCOID as _,
                self.inner.as_ptr(),
                pg_sys::Anum_pg_proc_proargdefaults as _,
                &mut is_null,
            );
            let proargdefaults = <&str>::from_datum(proargdefaults, is_null)?;

            let str = proargdefaults.as_pg_cstr();
            let argdefaults = mcx.exec_in(|| pg_sys::stringToNode(str)).cast::<pg_sys::List>();
            pg_sys::pfree(str.cast());
            List::downcast_ptr_in_memcx(argdefaults, mcx)
        }
    }

    #[inline]
    fn get_attr<T: FromDatum>(&self, attribute: u32) -> Option<T> {
        unsafe {
            // SAFETY:  SysCacheGetAttr will give us what we need to create a Datum of type T,
            // and this PgProc type ensures we have a valid "arg_tup" pointer for the cache entry
            let mut is_null = false;
            let datum = pg_sys::SysCacheGetAttr(
                pg_sys::SysCacheIdentifier::PROCOID as _,
                self.inner.as_ptr(),
                attribute as _,
                &mut is_null,
            );
            T::from_datum(datum, is_null)
        }
    }
}
