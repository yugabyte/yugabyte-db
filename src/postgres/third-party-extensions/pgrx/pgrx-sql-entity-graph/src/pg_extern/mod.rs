//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!

`#[pg_extern]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
mod argument;
mod attribute;
mod cast;
pub mod entity;
mod operator;
mod returning;
mod search_path;

pub use argument::PgExternArgument;
pub(crate) use attribute::Attribute;
pub use cast::PgCast;
pub use operator::PgOperator;
pub use returning::NameMacro;
use syn::token::Comma;

use self::returning::Returning;
use super::UsedType;
use crate::enrich::{CodeEnrichment, ToEntityGraphTokens, ToRustCodeTokens};
use crate::finfo::{finfo_v1_extern_c, finfo_v1_tokens};
use crate::fmt::ErrHarder;
use crate::ToSqlConfig;
use operator::{PgrxOperatorAttributeWithIdent, PgrxOperatorOpName};
use search_path::SearchPathList;

use proc_macro2::{Ident, Span, TokenStream as TokenStream2};
use quote::quote;
use syn::parse::{Parse, ParseStream, Parser};
use syn::punctuated::Punctuated;
use syn::spanned::Spanned;
use syn::{Meta, Token};

macro_rules! quote_spanned {
    ($span:expr=> $($expansion:tt)*) => {
        {
            let synthetic = Span::mixed_site();
            let synthetic = synthetic.located_at($span);
            quote::quote_spanned! {synthetic=> $($expansion)* }
        }
    };
}

macro_rules! format_ident {
    ($s:literal, $e:expr) => {{
        let mut synthetic = $e.clone();
        synthetic.set_span(Span::call_site().located_at($e.span()));
        quote::format_ident!($s, synthetic)
    }};
}

/// A parsed `#[pg_extern]` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a [`PgExternEntity`][crate::PgExternEntity].
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PgExtern;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PgExtern> = parse_quote! {
///     fn example(x: Option<str>) -> Option<&'a str> {
///         unimplemented!()
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct PgExtern {
    attrs: Vec<Attribute>,
    func: syn::ItemFn,
    to_sql_config: ToSqlConfig,
    operator: Option<PgOperator>,
    cast: Option<PgCast>,
    search_path: Option<SearchPathList>,
    inputs: Vec<PgExternArgument>,
    input_types: Vec<syn::Type>,
    returns: Returning,
}

impl PgExtern {
    #[track_caller]
    pub fn new(attr: TokenStream2, item: TokenStream2) -> Result<CodeEnrichment<Self>, syn::Error> {
        let mut attrs = Vec::new();
        let mut to_sql_config: Option<ToSqlConfig> = None;

        let parser = Punctuated::<Attribute, Token![,]>::parse_terminated;
        let attr_ts = attr.clone();
        let punctuated_attrs = parser
            .parse2(attr)
            .more_error(lazy_err!(attr_ts, "failed parsing pg_extern arguments"))?;
        for pair in punctuated_attrs.into_pairs() {
            match pair.into_value() {
                Attribute::Sql(config) => to_sql_config = to_sql_config.or(Some(config)),
                attr => attrs.push(attr),
            }
        }

        let mut to_sql_config = to_sql_config.unwrap_or_default();

        let func = syn::parse2::<syn::ItemFn>(item)?;

        if let Some(ref mut content) = to_sql_config.content {
            let value = content.value();
            // FIXME: find out if we should be using synthetic spans, issue #1667
            let span = content.span();
            let updated_value =
                value.replace("@FUNCTION_NAME@", &(func.sig.ident.to_string() + "_wrapper")) + "\n";
            *content = syn::LitStr::new(&updated_value, span);
        }

        if !to_sql_config.overrides_default() {
            crate::ident_is_acceptable_to_postgres(&func.sig.ident)?;
        }
        let operator = Self::operator(&func)?;
        let search_path = Self::search_path(&func)?;
        let inputs = Self::inputs(&func)?;
        let input_types = Self::input_types(&func)?;
        let returns = Returning::try_from(&func.sig.output)?;
        Ok(CodeEnrichment(Self {
            attrs,
            func,
            to_sql_config,
            operator,
            cast: None,
            search_path,
            inputs,
            input_types,
            returns,
        }))
    }

    /// Returns a new instance of this `PgExtern` with `cast` overwritten to `pg_cast`.
    pub fn as_cast(&self, pg_cast: PgCast) -> PgExtern {
        let mut result = self.clone();
        result.cast = Some(pg_cast);
        result
    }

    #[track_caller]
    fn input_types(func: &syn::ItemFn) -> syn::Result<Vec<syn::Type>> {
        func.sig
            .inputs
            .iter()
            .filter_map(|v| -> Option<syn::Result<syn::Type>> {
                match v {
                    syn::FnArg::Receiver(_) => None,
                    syn::FnArg::Typed(pat_ty) => {
                        let ty = match UsedType::new(*pat_ty.ty.clone()) {
                            Ok(v) => v.resolved_ty,
                            Err(e) => return Some(Err(e)),
                        };
                        Some(Ok(ty))
                    }
                }
            })
            .collect()
    }

    fn name(&self) -> String {
        self.attrs
            .iter()
            .find_map(|a| match a {
                Attribute::Name(name) => Some(name.value()),
                _ => None,
            })
            .unwrap_or_else(|| self.func.sig.ident.to_string())
    }

    fn schema(&self) -> Option<String> {
        self.attrs.iter().find_map(|a| match a {
            Attribute::Schema(name) => Some(name.value()),
            _ => None,
        })
    }

    pub fn extern_attrs(&self) -> &[Attribute] {
        self.attrs.as_slice()
    }

    #[track_caller]
    fn overridden(&self) -> Option<syn::LitStr> {
        let mut span = None;
        let mut retval = None;
        let mut in_commented_sql_block = false;
        for meta in self.func.attrs.iter().filter_map(|attr| {
            if attr.meta.path().is_ident("doc") {
                Some(attr.meta.clone())
            } else {
                None
            }
        }) {
            let Meta::NameValue(syn::MetaNameValue { ref value, .. }) = meta else { continue };
            let syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Str(inner), .. }) = value else {
                continue;
            };
            span.get_or_insert(value.span());
            if !in_commented_sql_block && inner.value().trim() == "```pgrxsql" {
                in_commented_sql_block = true;
            } else if in_commented_sql_block && inner.value().trim() == "```" {
                in_commented_sql_block = false;
            } else if in_commented_sql_block {
                let line = inner
                    .value()
                    .trim_start()
                    .replace("@FUNCTION_NAME@", &(self.func.sig.ident.to_string() + "_wrapper"))
                    + "\n";
                retval.get_or_insert_with(String::default).push_str(&line);
            }
        }
        retval.map(|s| syn::LitStr::new(s.as_ref(), span.unwrap()))
    }

    #[track_caller]
    fn operator(func: &syn::ItemFn) -> syn::Result<Option<PgOperator>> {
        let mut skel = Option::<PgOperator>::default();
        for attr in &func.attrs {
            let last_segment = attr.path().segments.last().unwrap();
            match last_segment.ident.to_string().as_str() {
                s @ "opname" => {
                    // we've been accepting strings of tokens for a while now
                    let attr = attr
                        .parse_args::<PgrxOperatorOpName>()
                        .more_error(lazy_err!(attr.clone(), "bad parse of {s}?"))?;
                    skel.get_or_insert_with(Default::default).opname.get_or_insert(attr);
                }
                s @ "commutator" => {
                    let attr: PgrxOperatorAttributeWithIdent = attr
                        .parse_args()
                        .more_error(lazy_err!(attr.clone(), "bad parse of {s}?"))?;

                    skel.get_or_insert_with(Default::default).commutator.get_or_insert(attr);
                }
                s @ "negator" => {
                    let attr: PgrxOperatorAttributeWithIdent = attr
                        .parse_args()
                        .more_error(lazy_err!(attr.clone(), "bad parse of {s}?"))?;
                    skel.get_or_insert_with(Default::default).negator.get_or_insert(attr);
                }
                s @ "join" => {
                    let attr: PgrxOperatorAttributeWithIdent = attr
                        .parse_args()
                        .more_error(lazy_err!(attr.clone(), "bad parse of {s}?"))?;

                    skel.get_or_insert_with(Default::default).join.get_or_insert(attr);
                }
                s @ "restrict" => {
                    let attr: PgrxOperatorAttributeWithIdent = attr
                        .parse_args()
                        .more_error(lazy_err!(attr.clone(), "bad parse of {s}?"))?;

                    skel.get_or_insert_with(Default::default).restrict.get_or_insert(attr);
                }
                "hashes" => {
                    skel.get_or_insert_with(Default::default).hashes = true;
                }
                "merges" => {
                    skel.get_or_insert_with(Default::default).merges = true;
                }
                _ => (),
            }
        }
        Ok(skel)
    }

    fn search_path(func: &syn::ItemFn) -> syn::Result<Option<SearchPathList>> {
        func.attrs
            .iter()
            .find(|f| {
                f.path()
                    .segments
                    .first()
                    // FIXME: find out if we should be using synthetic spans, issue #1667
                    .map(|f| f.ident == Ident::new("search_path", func.span()))
                    .unwrap_or_default()
            })
            .map(|attr| attr.parse_args::<SearchPathList>())
            .transpose()
    }

    fn inputs(func: &syn::ItemFn) -> syn::Result<Vec<PgExternArgument>> {
        let mut args = Vec::default();
        for input in &func.sig.inputs {
            let arg = PgExternArgument::build(input.clone())?;
            args.push(arg);
        }
        Ok(args)
    }

    fn entity_tokens(&self) -> TokenStream2 {
        let ident = &self.func.sig.ident;
        let name = self.name();
        let unsafety = &self.func.sig.unsafety;
        let schema = self.schema();
        let schema_iter = schema.iter();
        let extern_attrs = self
            .attrs
            .iter()
            .map(|attr| attr.to_sql_entity_graph_tokens())
            .collect::<Punctuated<_, Token![,]>>();
        let search_path = self.search_path.clone().into_iter();
        let inputs = &self.inputs;
        let inputs_iter = inputs.iter().map(|v| v.entity_tokens());

        let input_types = self.input_types.iter().cloned();

        let returns = &self.returns;

        let return_type = match &self.func.sig.output {
            syn::ReturnType::Default => None,
            syn::ReturnType::Type(arrow, ty) => {
                let ty = ty.clone();
                Some(syn::ReturnType::Type(*arrow, ty))
            }
        };

        let operator = self.operator.clone().into_iter();
        let cast = self.cast.clone().into_iter();
        let to_sql_config = match self.overridden() {
            None => self.to_sql_config.clone(),
            Some(content) => ToSqlConfig { content: Some(content), ..self.to_sql_config.clone() },
        };

        let lifetimes = self
            .func
            .sig
            .generics
            .params
            .iter()
            .filter_map(|generic| match generic {
                // syn::GenericParam::Type(_) => todo!(),
                syn::GenericParam::Lifetime(lt) => Some(lt),
                _ => None, // syn::GenericParam::Const(_) => todo!(),
            })
            .collect::<Vec<_>>();
        let hrtb = if lifetimes.is_empty() { None } else { Some(quote! { for<#(#lifetimes),*> }) };

        let sql_graph_entity_fn_name = format_ident!("__pgrx_internals_fn_{}", ident);
        quote_spanned! { self.func.sig.span() =>
            #[no_mangle]
            #[doc(hidden)]
            #[allow(unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn  #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                extern crate alloc;
                #[allow(unused_imports)]
                use alloc::{vec, vec::Vec};
                type FunctionPointer = #hrtb #unsafety fn(#( #input_types ),*) #return_type;
                let submission = ::pgrx::pgrx_sql_entity_graph::PgExternEntity {
                    name: #name,
                    unaliased_name: stringify!(#ident),
                    module_path: core::module_path!(),
                    full_path: concat!(core::module_path!(), "::", stringify!(#ident)),
                    metadata: <FunctionPointer as ::pgrx::pgrx_sql_entity_graph::metadata::FunctionMetadata::<_>>::entity(),
                    fn_args: vec![#(#inputs_iter),*],
                    fn_return: #returns,
                    #[allow(clippy::or_fun_call)]
                    schema: None #( .unwrap_or_else(|| Some(#schema_iter)) )*,
                    file: file!(),
                    line: line!(),
                    extern_attrs: vec![#extern_attrs],
                    #[allow(clippy::or_fun_call)]
                    search_path: None #( .unwrap_or_else(|| Some(vec![#search_path])) )*,
                    #[allow(clippy::or_fun_call)]
                    operator: None #( .unwrap_or_else(|| Some(#operator)) )*,
                    cast: None #( .unwrap_or_else(|| Some(#cast)) )*,
                    to_sql_config: #to_sql_config,
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::Function(submission)
            }
        }
    }

    pub fn wrapper_func(&self) -> Result<syn::ItemFn, syn::Error> {
        let signature = &self.func.sig;
        let func_name = &signature.ident;
        // we do this odd dance so we can pass the same ident to macros that don't know each other
        let synthetic_ident_span = Span::mixed_site().located_at(signature.ident.span());
        let fcinfo_ident = syn::Ident::new("fcinfo", synthetic_ident_span);
        let mut lifetimes = signature
            .generics
            .lifetimes()
            .cloned()
            .collect::<syn::punctuated::Punctuated<_, Comma>>();
        // we pick an arbitrary lifetime from the provided signature of the fn, if available,
        // so lifetime-bound fn are easier to write with pgrx
        let fc_lt = lifetimes
            .first()
            .map(|lt_p| lt_p.lifetime.clone())
            .filter(|lt| lt.ident != "static")
            .unwrap_or(syn::Lifetime::new("'fcx", Span::mixed_site()));
        // we need the bare lifetime later, but we also jam it into the bounds if it is new
        let fc_ltparam = syn::LifetimeParam::new(fc_lt.clone());
        if lifetimes.first() != Some(&fc_ltparam) {
            lifetimes.insert(0, fc_ltparam)
        }

        let args = &self.inputs;
        // for unclear reasons the linker vomits if we don't do this
        let arg_pats = args.iter().map(|v| format_ident!("{}_", &v.pat)).collect::<Vec<_>>();
        let args_ident = proc_macro2::Ident::new("_args", Span::call_site());
        let arg_fetches = arg_pats.iter().map(|pat| {
                quote_spanned!{ pat.span() =>
                    let #pat = #args_ident.next_arg_unchecked().unwrap_or_else(|| panic!("unboxing {} argument failed", stringify!(#pat)));
                }
            }
        );

        match &self.returns {
            Returning::None
            | Returning::Type(_)
            | Returning::SetOf { .. }
            | Returning::Iterated { .. } => {
                let ret_ty = match &signature.output {
                    syn::ReturnType::Default => syn::parse_quote! { () },
                    syn::ReturnType::Type(_, ret_ty) => ret_ty.clone(),
                };
                let wrapper_code = quote_spanned! { self.func.block.span() =>
                    fn _internal_wrapper<#lifetimes>(fcinfo: &mut ::pgrx::callconv::FcInfo<#fc_lt>) -> ::pgrx::datum::Datum<#fc_lt> {
                        #[allow(unused_unsafe)]
                        unsafe {
                            let call_flow = <#ret_ty as ::pgrx::callconv::RetAbi>::check_and_prepare(fcinfo);
                            let result = match call_flow {
                                ::pgrx::callconv::CallCx::WrappedFn(mcx) => {
                                    let mut mcx = ::pgrx::PgMemoryContexts::For(mcx);
                                    let #args_ident = &mut fcinfo.args();
                                    let call_result = mcx.switch_to(|_| {
                                        #(#arg_fetches)*
                                        #func_name( #(#arg_pats),* )
                                    });
                                    ::pgrx::callconv::RetAbi::to_ret(call_result)
                                }
                                ::pgrx::callconv::CallCx::RestoreCx => <#ret_ty as ::pgrx::callconv::RetAbi>::ret_from_fcx(fcinfo),
                            };
                            unsafe { <#ret_ty as ::pgrx::callconv::RetAbi>::box_ret_in(fcinfo, result) }
                        }
                    }
                    // We preserve the invariants
                    let datum = unsafe {
                        ::pgrx::pg_sys::submodules::panic::pgrx_extern_c_guard(|| {
                            let mut fcinfo = ::pgrx::callconv::FcInfo::from_ptr(#fcinfo_ident);
                            _internal_wrapper(&mut fcinfo)
                        })
                    };
                    datum.sans_lifetime()
                };
                finfo_v1_extern_c(&self.func, fcinfo_ident, wrapper_code)
            }
        }
    }
}

trait LastIdent {
    fn filter_last_ident(&self, id: &str) -> Option<&syn::PathSegment>;
    #[inline]
    fn last_ident_is(&self, id: &str) -> bool {
        self.filter_last_ident(id).is_some()
    }
}

impl LastIdent for syn::Type {
    #[inline]
    fn filter_last_ident(&self, id: &str) -> Option<&syn::PathSegment> {
        let syn::Type::Path(syn::TypePath { path, .. }) = self else { return None };
        path.filter_last_ident(id)
    }
}
impl LastIdent for syn::TypePath {
    #[inline]
    fn filter_last_ident(&self, id: &str) -> Option<&syn::PathSegment> {
        self.path.filter_last_ident(id)
    }
}
impl LastIdent for syn::Path {
    #[inline]
    fn filter_last_ident(&self, id: &str) -> Option<&syn::PathSegment> {
        self.segments.filter_last_ident(id)
    }
}
impl<P> LastIdent for Punctuated<syn::PathSegment, P> {
    #[inline]
    fn filter_last_ident(&self, id: &str) -> Option<&syn::PathSegment> {
        self.last().filter(|segment| segment.ident == id)
    }
}

impl ToEntityGraphTokens for PgExtern {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        self.entity_tokens()
    }
}

impl ToRustCodeTokens for PgExtern {
    fn to_rust_code_tokens(&self) -> TokenStream2 {
        let original_func = &self.func;
        let wrapper_func = self.wrapper_func().unwrap();
        let finfo_tokens = finfo_v1_tokens(wrapper_func.sig.ident.clone()).unwrap();

        quote_spanned! { self.func.sig.span() =>
            #original_func
            #wrapper_func
            #finfo_tokens
        }
    }
}

impl Parse for CodeEnrichment<PgExtern> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let parser = Punctuated::<Attribute, Token![,]>::parse_terminated;
        let punctuated_attrs = input.call(parser).ok().unwrap_or_default();
        let attrs = punctuated_attrs.into_pairs().map(|pair| pair.into_value());
        PgExtern::new(quote! {#(#attrs)*}, input.parse()?)
    }
}
