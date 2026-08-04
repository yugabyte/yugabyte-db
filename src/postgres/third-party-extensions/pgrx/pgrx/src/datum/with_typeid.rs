use crate::datum::{Array, FromDatum, PgVarlena, VariadicArray};
use crate::PgBox;
use core::any::TypeId;
use once_cell::sync::Lazy;
use pgrx_sql_entity_graph::RustSqlMapping;

/// Obtain a TypeId for T without `T: 'static`
#[inline]
#[doc(hidden)]
pub fn nonstatic_typeid<T: ?Sized>() -> core::any::TypeId {
    trait NonStaticAny {
        fn type_id(&self) -> core::any::TypeId
        where
            Self: 'static;
    }
    impl<T: ?Sized> NonStaticAny for core::marker::PhantomData<T> {
        #[inline]
        fn type_id(&self) -> core::any::TypeId
        where
            Self: 'static,
        {
            core::any::TypeId::of::<T>()
        }
    }
    let it = core::marker::PhantomData::<T>;
    // There is no excuse for the crimes we have done here, but what jury would convict us?
    unsafe { core::mem::transmute::<&dyn NonStaticAny, &'static dyn NonStaticAny>(&it).type_id() }
}

/// A type which can have its [`core::any::TypeId`]s registered for Rust to SQL mapping.
///
/// An example use of this trait:
///
/// ```rust
/// use pgrx::prelude::*;
/// use serde::{Serialize, Deserialize};
///
/// #[derive(Debug, Clone, Copy, Serialize, Deserialize, PostgresType)]
/// struct Treat<'a> { best_part: &'a str, };
///
/// let mut mappings = Default::default();
/// let treat_string = stringify!(Treat).to_string();
/// <Treat<'_> as pgrx::datum::WithTypeIds>::register_with_refs(&mut mappings, treat_string.clone());
///
/// # assert!(mappings.iter().any(|x| x.id == ::pgrx::datum::nonstatic_typeid::<Treat<'static>>()));
/// ```
///
/// This trait uses the fact that inherent implementations are a higher priority than trait
/// implementations.
pub trait WithTypeIds {
    const ITEM_ID: Lazy<TypeId>;
    const OPTION_ID: Lazy<Option<TypeId>>;
    const VEC_ID: Lazy<Option<TypeId>>;
    const VEC_OPTION_ID: Lazy<Option<TypeId>>;
    const OPTION_VEC_ID: Lazy<Option<TypeId>>;
    const OPTION_VEC_OPTION_ID: Lazy<Option<TypeId>>;
    const ARRAY_ID: Lazy<Option<TypeId>>;
    const OPTION_ARRAY_ID: Lazy<Option<TypeId>>;
    const VARIADICARRAY_ID: Lazy<Option<TypeId>>;
    const OPTION_VARIADICARRAY_ID: Lazy<Option<TypeId>>;
    const VARLENA_ID: Lazy<Option<TypeId>>;
    const OPTION_VARLENA_ID: Lazy<Option<TypeId>>;

    fn register_with_refs(map: &mut std::collections::HashSet<RustSqlMapping>, single_sql: String) {
        Self::register(map, single_sql.clone());
        <&Self as WithTypeIds>::register(map, single_sql.clone());
        <&mut Self as WithTypeIds>::register(map, single_sql);
    }

    fn register_sized_with_refs(
        _map: &mut std::collections::HashSet<RustSqlMapping>,
        _single_sql: String,
    ) {
        ()
    }

    fn register_sized(_map: &mut std::collections::HashSet<RustSqlMapping>, _single_sql: String) {
        ()
    }

    fn register_varlena_with_refs(
        _map: &mut std::collections::HashSet<RustSqlMapping>,
        _single_sql: String,
    ) {
        ()
    }

    fn register_varlena(_map: &mut std::collections::HashSet<RustSqlMapping>, _single_sql: String) {
        ()
    }

    fn register_array_with_refs(
        _map: &mut std::collections::HashSet<RustSqlMapping>,
        _single_sql: String,
    ) {
        ()
    }

    fn register_array(_map: &mut std::collections::HashSet<RustSqlMapping>, _single_sql: String) {
        ()
    }

    fn register(set: &mut std::collections::HashSet<RustSqlMapping>, single_sql: String) {
        let rust = core::any::type_name::<Self>();
        assert!(
            set.insert(RustSqlMapping {
                sql: single_sql.clone(),
                rust: rust.to_string(),
                id: *Self::ITEM_ID,
            }),
            "Cannot set mapping of `{rust}` twice, was already `{single_sql}`.",
        );
    }
}

impl<T: ?Sized> WithTypeIds for T {
    const ITEM_ID: Lazy<TypeId> = Lazy::new(|| nonstatic_typeid::<T>());
    const OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const VEC_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const VEC_OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const OPTION_VEC_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const OPTION_VEC_OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const ARRAY_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const OPTION_ARRAY_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const VARIADICARRAY_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const OPTION_VARIADICARRAY_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const VARLENA_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    const OPTION_VARLENA_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
}

/// A type which can have its [`core::any::TypeId`]s registered for Rust to SQL mapping.
///
/// An example use of this trait:
///
/// ```rust
/// use pgrx::prelude::*;
/// use serde::{Serialize, Deserialize};
///
/// #[derive(Debug, Clone, Copy, Serialize, Deserialize, PostgresType)]
/// pub struct Treat<'a> { best_part: &'a str, };
///
/// let mut mappings = Default::default();
/// let treat_string = stringify!(Treat).to_string();
///
/// pgrx::datum::WithSizedTypeIds::<Treat<'static>>::register_sized_with_refs(
///     &mut mappings,
///     treat_string.clone()
/// );
///
/// assert!(mappings.iter().any(|x| x.id == core::any::TypeId::of::<Option<Treat<'static>>>()));
/// ```
///
/// This trait uses the fact that inherent implementations are a higher priority than trait
/// implementations.
pub struct WithSizedTypeIds<T>(pub core::marker::PhantomData<T>);

impl<T> WithSizedTypeIds<T> {
    pub const PG_BOX_ID: Lazy<Option<TypeId>> = Lazy::new(|| Some(nonstatic_typeid::<PgBox<T>>()));
    pub const PG_BOX_OPTION_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<PgBox<Option<T>>>()));
    pub const PG_BOX_VEC_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<PgBox<Vec<T>>>()));
    pub const OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(|| Some(nonstatic_typeid::<Option<T>>()));
    pub const VEC_ID: Lazy<Option<TypeId>> = Lazy::new(|| Some(nonstatic_typeid::<Vec<T>>()));
    pub const VEC_OPTION_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Vec<Option<T>>>()));
    pub const OPTION_VEC_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Option<Vec<T>>>()));
    pub const OPTION_VEC_OPTION_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Option<Vec<Option<T>>>>()));

    pub fn register_sized_with_refs(
        map: &mut std::collections::HashSet<RustSqlMapping>,
        single_sql: String,
    ) where
        Self: 'static,
    {
        WithSizedTypeIds::<T>::register_sized(map, single_sql.clone());
        WithSizedTypeIds::<&T>::register_sized(map, single_sql.clone());
        WithSizedTypeIds::<&mut T>::register_sized(map, single_sql);
    }

    pub fn register_sized(map: &mut std::collections::HashSet<RustSqlMapping>, single_sql: String) {
        let set_sql = format!("{single_sql}[]");

        if let Some(id) = *WithSizedTypeIds::<T>::PG_BOX_ID {
            let rust = core::any::type_name::<crate::PgBox<T>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithSizedTypeIds::<T>::PG_BOX_OPTION_ID {
            let rust = core::any::type_name::<crate::PgBox<Option<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithSizedTypeIds::<T>::PG_BOX_VEC_ID {
            let rust = core::any::type_name::<crate::PgBox<Vec<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithSizedTypeIds::<T>::OPTION_ID {
            let rust = core::any::type_name::<Option<T>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithSizedTypeIds::<T>::VEC_ID {
            let rust = core::any::type_name::<T>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithSizedTypeIds::<T>::VEC_OPTION_ID {
            let rust = core::any::type_name::<Vec<Option<T>>>();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithSizedTypeIds::<T>::OPTION_VEC_ID {
            let rust = core::any::type_name::<Option<Vec<T>>>();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithSizedTypeIds::<T>::OPTION_VEC_OPTION_ID {
            let rust = core::any::type_name::<Option<Vec<Option<T>>>>();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
    }
}

/// An [`Array`] compatible type which can have its [`core::any::TypeId`]s registered for Rust to SQL mapping.
///
/// An example use of this trait:
///
/// ```rust
/// use pgrx::prelude::*;
/// use serde::{Serialize, Deserialize};
///
/// #[derive(Debug, Clone, Serialize, Deserialize, PostgresType)]
/// pub struct Treat { best_part: String, };
///
/// let mut mappings = Default::default();
/// let treat_string = stringify!(Treat).to_string();
///
/// pgrx::datum::WithArrayTypeIds::<Treat>::register_array_with_refs(
///     &mut mappings,
///     treat_string.clone()
/// );
///
/// # assert!(mappings.iter().any(|x| x.id == ::pgrx::datum::nonstatic_typeid::<Array<Treat>>()));
/// ```
///
/// This trait uses the fact that inherent implementations are a higher priority than trait
/// implementations.
pub struct WithArrayTypeIds<T>(pub core::marker::PhantomData<T>);

impl<T: FromDatum + 'static> WithArrayTypeIds<T> {
    pub const ARRAY_ID: Lazy<Option<TypeId>> = Lazy::new(|| Some(nonstatic_typeid::<Array<T>>()));
    pub const OPTION_ARRAY_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Option<Array<T>>>()));
    pub const VARIADICARRAY_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<VariadicArray<T>>()));
    pub const OPTION_VARIADICARRAY_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Option<VariadicArray<T>>>()));

    pub fn register_array_with_refs(
        map: &mut std::collections::HashSet<RustSqlMapping>,
        single_sql: String,
    ) where
        Self: 'static,
    {
        WithArrayTypeIds::<T>::register_array(map, single_sql.clone());
        WithArrayTypeIds::<&T>::register_array(map, single_sql.clone());
        WithArrayTypeIds::<&mut T>::register_array(map, single_sql);
    }

    pub fn register_array(map: &mut std::collections::HashSet<RustSqlMapping>, single_sql: String) {
        let set_sql = format!("{single_sql}[]");

        if let Some(id) = *WithArrayTypeIds::<T>::ARRAY_ID {
            let rust = core::any::type_name::<Array<T>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithArrayTypeIds::<T>::OPTION_ARRAY_ID {
            let rust = core::any::type_name::<Option<Array<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithArrayTypeIds::<T>::VARIADICARRAY_ID {
            let rust = core::any::type_name::<VariadicArray<T>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithArrayTypeIds::<T>::OPTION_VARIADICARRAY_ID {
            let rust = core::any::type_name::<Option<VariadicArray<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: set_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
    }
}

/// A [`PgVarlena`] compatible type which can have its [`core::any::TypeId`]s registered for Rust to SQL mapping.
///
/// An example use of this trait:
///
/// ```rust
/// use pgrx::prelude::*;
/// use serde::{Serialize, Deserialize};
///
/// #[derive(Debug, Clone, Copy, Serialize, Deserialize, PostgresType)]
/// pub struct Treat<'a> { best_part: &'a str, };
///
/// let mut mappings = Default::default();
/// let treat_string = stringify!(Treat).to_string();
///
/// pgrx::datum::WithVarlenaTypeIds::<Treat<'static>>::register_varlena_with_refs(
///     &mut mappings,
///     treat_string.clone()
/// );
///
/// # assert!(mappings.iter().any(|x| x.id == ::pgrx::datum::nonstatic_typeid::<PgVarlena<Treat<'_>>>()));
/// ```
///
/// This trait uses the fact that inherent implementations are a higher priority than trait
/// implementations.
pub struct WithVarlenaTypeIds<T>(pub core::marker::PhantomData<T>);

impl<T: Copy + 'static> WithVarlenaTypeIds<T> {
    pub const VARLENA_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<PgVarlena<T>>()));
    pub const PG_BOX_VARLENA_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<PgBox<PgVarlena<T>>>()));
    pub const OPTION_VARLENA_ID: Lazy<Option<TypeId>> =
        Lazy::new(|| Some(nonstatic_typeid::<Option<PgVarlena<T>>>()));

    pub fn register_varlena_with_refs(
        map: &mut std::collections::HashSet<RustSqlMapping>,
        single_sql: String,
    ) where
        Self: 'static,
    {
        WithVarlenaTypeIds::<T>::register_varlena(map, single_sql.clone());
        WithVarlenaTypeIds::<&T>::register_varlena(map, single_sql.clone());
        WithVarlenaTypeIds::<&mut T>::register_varlena(map, single_sql);
    }

    pub fn register_varlena(
        map: &mut std::collections::HashSet<RustSqlMapping>,
        single_sql: String,
    ) {
        if let Some(id) = *WithVarlenaTypeIds::<T>::VARLENA_ID {
            let rust = core::any::type_name::<PgVarlena<T>>();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }

        if let Some(id) = *WithVarlenaTypeIds::<T>::PG_BOX_VARLENA_ID {
            let rust = core::any::type_name::<PgBox<PgVarlena<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
        if let Some(id) = *WithVarlenaTypeIds::<T>::OPTION_VARLENA_ID {
            let rust = core::any::type_name::<Option<PgVarlena<T>>>().to_string();
            assert!(
                map.insert(RustSqlMapping { sql: single_sql.clone(), rust: rust.to_string(), id }),
                "Cannot map `{rust}` twice.",
            );
        }
    }
}
