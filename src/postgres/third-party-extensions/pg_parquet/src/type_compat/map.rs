use std::ffi::CStr;

use once_cell::sync::OnceCell;
use pgrx::{
    datum::UnboxDatum,
    pg_sys::{
        format_type_be_qualified, get_extension_oid, Anum_pg_type_oid, AsPgCStr, Datum,
        GetSysCacheOid, InvalidOid, Oid, SysCacheIdentifier::TYPEOID,
    },
    prelude::PgHeapTuple,
    AllocatedByRust, FromDatum, IntoDatum,
};

use crate::pgrx_utils::is_domain_of_array_type;

// we need to reset the map context at each copy start
static mut MAP_CONTEXT: OnceCell<MapExtensionContext> = OnceCell::new();

fn get_map_context() -> &'static mut MapExtensionContext {
    #[allow(static_mut_refs)]
    unsafe {
        MAP_CONTEXT
            .get_mut()
            .expect("map context is not initialized")
    }
}

pub(crate) fn reset_map_context() {
    #[allow(static_mut_refs)]
    unsafe {
        MAP_CONTEXT.take()
    };

    #[allow(static_mut_refs)]
    unsafe {
        MAP_CONTEXT
            .set(MapExtensionContext::new())
            .expect("failed to reset map context")
    };
}

pub(crate) fn reset_map_type_context(map_type_oid: Oid) {
    get_map_context()
        .map_type_context
        .set_current_map_type_oid(map_type_oid);
}

pub(crate) fn is_map_type(typoid: Oid) -> bool {
    let map_context = get_map_context();

    if map_context.map_extension_oid.is_none() {
        return false;
    }

    // map is a domain type over array of key-value pairs
    if !is_domain_of_array_type(typoid) {
        return false;
    }

    let type_name = unsafe { format_type_be_qualified(typoid) };
    let type_name = unsafe { CStr::from_ptr(type_name).to_str().unwrap() };

    if !type_name.starts_with("crunchy_map.") {
        return false;
    }

    let map_typoid = unsafe {
        GetSysCacheOid(
            TYPEOID as _,
            Anum_pg_type_oid as _,
            typoid.into_datum().unwrap(),
            Datum::from(0), // not used key
            Datum::from(0), // not used key
            Datum::from(0), // not used key
        )
    };

    map_typoid != InvalidOid
}

#[derive(Debug, PartialEq, Clone)]
struct MapTypeContext {
    current_map_type_oid: Option<Oid>,
}

impl MapTypeContext {
    fn set_current_map_type_oid(&mut self, typoid: Oid) {
        self.current_map_type_oid = Some(typoid);
    }
}

#[derive(Debug, PartialEq, Clone)]
struct MapExtensionContext {
    map_extension_oid: Option<Oid>,
    map_type_context: MapTypeContext,
}

impl MapExtensionContext {
    fn new() -> Self {
        let map_extension_oid = unsafe { get_extension_oid("crunchy_map".as_pg_cstr(), true) };
        let map_extension_oid = if map_extension_oid == InvalidOid {
            None
        } else {
            Some(map_extension_oid)
        };

        Self {
            map_extension_oid,
            map_type_context: MapTypeContext {
                current_map_type_oid: None,
            },
        }
    }
}

// crunchy_map is a domain type over array of key-value pairs
pub(crate) struct Map<'a> {
    pub(crate) entries: pgrx::Array<'a, PgHeapTuple<'a, AllocatedByRust>>,
}

impl IntoDatum for Map<'_> {
    fn into_datum(self) -> Option<Datum> {
        // since the map is stored as an array of tuples, we can simply convert the array to a datum
        self.entries.into_datum()
    }

    fn type_oid() -> Oid {
        get_map_context()
            .map_type_context
            .current_map_type_oid
            .expect("map type context is not initialized")
    }
}

impl FromDatum for Map<'_> {
    unsafe fn from_polymorphic_datum(datum: Datum, is_null: bool, _typoid: Oid) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            let is_null = false;
            let entries = pgrx::Array::<PgHeapTuple<AllocatedByRust>>::from_datum(datum, is_null)
                .expect("cannot convert datum to map entries");

            Some(Map { entries })
        }
    }
}

unsafe impl UnboxDatum for Map<'_> {
    type As<'src>
        = Self
    where
        Self: 'src;

    unsafe fn unbox<'src>(datum: pgrx::datum::Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        let is_null = false;
        let entries =
            pgrx::Array::<PgHeapTuple<AllocatedByRust>>::from_datum(datum.sans_lifetime(), is_null)
                .expect("cannot convert datum to map entries");

        Map { entries }
    }
}
