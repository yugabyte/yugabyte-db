//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx_sql_entity_graph::{PostgresHash, PostgresOrd};

use crate::{parse_postgres_type_args, PostgresTypeAttribute};
use proc_macro2::Ident;
use quote::{quote, ToTokens};
use syn::DeriveInput;

#[track_caller]
fn ident_and_path(ast: &DeriveInput) -> (&Ident, proc_macro2::TokenStream) {
    let ident = &ast.ident;
    let args = parse_postgres_type_args(&ast.attrs);
    let path = if args.contains(&PostgresTypeAttribute::PgVarlenaInOutFuncs) {
        quote! { ::pgrx::datum::PgVarlena<#ident> }
    } else {
        quote! { #ident }
    };
    (ident, path)
}

#[track_caller]
pub(crate) fn deriving_postgres_eq(ast: DeriveInput) -> syn::Result<proc_macro2::TokenStream> {
    let mut stream = proc_macro2::TokenStream::new();
    let (ident, path) = ident_and_path(&ast);
    stream.extend(derive_pg_eq(ident, &path));
    stream.extend(derive_pg_ne(ident, &path));

    Ok(stream)
}

#[track_caller]
pub(crate) fn deriving_postgres_ord(ast: DeriveInput) -> syn::Result<proc_macro2::TokenStream> {
    let mut stream = proc_macro2::TokenStream::new();
    let (ident, path) = ident_and_path(&ast);

    stream.extend(derive_pg_lt(ident, &path));
    stream.extend(derive_pg_gt(ident, &path));
    stream.extend(derive_pg_le(ident, &path));
    stream.extend(derive_pg_ge(ident, &path));
    stream.extend(derive_pg_cmp(ident, &path));

    let sql_graph_entity_item = PostgresOrd::from_derive_input(ast)?;
    sql_graph_entity_item.to_tokens(&mut stream);

    Ok(stream)
}

pub(crate) fn deriving_postgres_hash(ast: DeriveInput) -> syn::Result<proc_macro2::TokenStream> {
    let mut stream = proc_macro2::TokenStream::new();
    let (ident, path) = ident_and_path(&ast);

    stream.extend(derive_pg_hash(ident, &path));

    let sql_graph_entity_item = PostgresHash::from_derive_input(ast)?;
    sql_graph_entity_item.to_tokens(&mut stream);

    Ok(stream)
}

/// Derive a Postgres `=` operator from Rust `==`
///
/// Note this expansion applies a number of assumptions that may not be true:
/// - PartialEq::eq is referentially transparent (immutable and parallel-safe)
/// - PartialEq::ne must reverse PartialEq::eq (negator)
/// - PartialEq::eq is commutative
///
/// Postgres swears that these are just ["optimization hints"], and they can be
/// defined to use regular SQL or PL/pgSQL functions with spurious results.
///
/// However, it is entirely plausible these assumptions actually are venomous.
/// It is deeply unlikely that we can audit the millions of lines of C code in
/// Postgres to confirm that it avoids using these assumptions in a way that
/// would lead to UB or unacceptable behavior from PGRX if Eq is incorrectly
/// implemented, and we have no realistic means of guaranteeing this.
///
/// Further, Postgres adds a disclaimer to these "optimization hints":
///
/// > But if you provide them, you must be sure that they are right!
/// > Incorrect use of an optimization clause can result in
/// > slow queries, subtly wrong output, or other Bad Things.
///
/// In practice, most Eq impls are in fact correct, referentially transparent,
/// and commutative. So this note could be for nothing. This signpost is left
/// in order to guide anyone unfortunate enough to be debugging an issue that
/// finally leads them here.
///
/// ["optimization hints"]: https://www.postgresql.org/docs/current/xoper-optimization.html
#[track_caller]
pub fn derive_pg_eq(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_eq").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(=)]
        #[::pgrx::pgrx_macros::commutator(=)]
        #[::pgrx::pgrx_macros::negator(<>)]
        #[::pgrx::pgrx_macros::restrict(eqsel)]
        #[::pgrx::pgrx_macros::join(eqjoinsel)]
        #[::pgrx::pgrx_macros::merges]
        #[::pgrx::pgrx_macros::hashes]
        fn #pg_name(left: #path, right: #path) -> bool
        where
            #path: ::core::cmp::Eq,
        {
            left == right
        }
    }
}

/// Derive a Postgres `<>` operator from Rust `!=`
///
/// Note that this expansion applies a number of assumptions that aren't necessarily true:
/// - PartialEq::ne is referentially transparent (immutable and parallel-safe)
/// - PartialEq::eq must reverse PartialEq::ne (negator)
/// - PartialEq::ne is commutative
///
/// See `derive_pg_eq` for the implications of this assumption.
#[track_caller]
pub fn derive_pg_ne(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_ne").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(<>)]
        #[::pgrx::pgrx_macros::commutator(<>)]
        #[::pgrx::pgrx_macros::negator(=)]
        #[::pgrx::pgrx_macros::restrict(neqsel)]
        #[::pgrx::pgrx_macros::join(neqjoinsel)]
        fn #pg_name(left: #path, right: #path) -> bool {
            left != right
        }
    }
}

#[track_caller]
pub fn derive_pg_lt(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_lt").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(<)]
        #[::pgrx::pgrx_macros::negator(>=)]
        #[::pgrx::pgrx_macros::commutator(>)]
        #[::pgrx::pgrx_macros::restrict(scalarltsel)]
        #[::pgrx::pgrx_macros::join(scalarltjoinsel)]
        fn #pg_name(left: #path, right: #path) -> bool {
            left < right
        }

    }
}

#[track_caller]
pub fn derive_pg_gt(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_gt").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(>)]
        #[::pgrx::pgrx_macros::negator(<=)]
        #[::pgrx::pgrx_macros::commutator(<)]
        #[::pgrx::pgrx_macros::restrict(scalargtsel)]
        #[::pgrx::pgrx_macros::join(scalargtjoinsel)]
        fn #pg_name(left: #path, right: #path) -> bool {
            left > right
        }
    }
}

#[track_caller]
pub fn derive_pg_le(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_le").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(<=)]
        #[::pgrx::pgrx_macros::negator(>)]
        #[::pgrx::pgrx_macros::commutator(>=)]
        #[::pgrx::pgrx_macros::restrict(scalarlesel)]
        #[::pgrx::pgrx_macros::join(scalarlejoinsel)]
        fn #pg_name(left: #path, right: #path) -> bool {
            left <= right
        }
    }
}

#[track_caller]
pub fn derive_pg_ge(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_ge").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_operator(immutable, parallel_safe)]
        #[::pgrx::pgrx_macros::opname(>=)]
        #[::pgrx::pgrx_macros::negator(<)]
        #[::pgrx::pgrx_macros::commutator(<=)]
        #[::pgrx::pgrx_macros::restrict(scalargesel)]
        #[::pgrx::pgrx_macros::join(scalargejoinsel)]
        fn #pg_name(left: #path, right: #path) -> bool {
            left >= right
        }
    }
}

#[track_caller]
pub fn derive_pg_cmp(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_cmp").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_extern(immutable, parallel_safe)]
        fn #pg_name(left: #path, right: #path) -> i32 {
            ::core::cmp::Ord::cmp(&left, &right) as i32
        }
    }
}

/// Derive a Postgres hash operator using a provided hash function
///
/// # HashEq?
///
/// To quote the std documentation:
///
/// "When implementing both Hash and Eq, it is important that the following property holds:
/// ```text
/// k1 == k2 -> hash(k1) == hash(k2)
/// ```
/// In other words, if two keys are equal, their hashes must also be equal. HashMap and HashSet both rely on this behavior."
///
/// Postgres is no different: this hashing is for the explicit purpose of equality checks,
/// and it also needs to be able to reason from hash equality to actual equality.
#[track_caller]
pub fn derive_pg_hash(name: &Ident, path: &proc_macro2::TokenStream) -> proc_macro2::TokenStream {
    let pg_name = Ident::new(&format!("{name}_hash").to_lowercase(), name.span());
    quote! {
        #[allow(non_snake_case)]
        #[::pgrx::pgrx_macros::pg_extern(immutable, parallel_safe)]
        fn #pg_name(value: #path) -> i32
        where
            #path: ::core::hash::Hash + ::core::cmp::Eq,
        {
            ::pgrx::misc::pgrx_seahash(&value) as i32
        }
    }
}
