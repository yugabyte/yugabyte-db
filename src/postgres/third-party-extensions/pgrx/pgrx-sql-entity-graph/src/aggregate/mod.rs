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

`#[pg_aggregate]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/
mod aggregate_type;
pub(crate) mod entity;
mod options;

pub use aggregate_type::{AggregateType, AggregateTypeList};
pub use options::{FinalizeModify, ParallelOption};

use crate::enrich::CodeEnrichment;
use crate::enrich::ToEntityGraphTokens;
use crate::enrich::ToRustCodeTokens;
use convert_case::{Case, Casing};
use proc_macro2::{Ident, Span, TokenStream as TokenStream2};
use quote::quote;
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::spanned::Spanned;
use syn::{
    parse_quote, Expr, ImplItemConst, ImplItemFn, ImplItemType, ItemFn, ItemImpl, Path, Type,
};

use crate::ToSqlConfig;

use super::UsedType;

// We support only 32 tuples...
const ARG_NAMES: [&str; 32] = [
    "arg_one",
    "arg_two",
    "arg_three",
    "arg_four",
    "arg_five",
    "arg_six",
    "arg_seven",
    "arg_eight",
    "arg_nine",
    "arg_ten",
    "arg_eleven",
    "arg_twelve",
    "arg_thirteen",
    "arg_fourteen",
    "arg_fifteen",
    "arg_sixteen",
    "arg_seventeen",
    "arg_eighteen",
    "arg_nineteen",
    "arg_twenty",
    "arg_twenty_one",
    "arg_twenty_two",
    "arg_twenty_three",
    "arg_twenty_four",
    "arg_twenty_five",
    "arg_twenty_six",
    "arg_twenty_seven",
    "arg_twenty_eight",
    "arg_twenty_nine",
    "arg_thirty",
    "arg_thirty_one",
    "arg_thirty_two",
];

/** A parsed `#[pg_aggregate]` item.
*/
#[derive(Debug, Clone)]
pub struct PgAggregate {
    item_impl: ItemImpl,
    name: Expr,
    target_ident: Ident,
    pg_externs: Vec<ItemFn>,
    // Note these should not be considered *writable*, they're snapshots from construction.
    type_args: AggregateTypeList,
    type_ordered_set_args: Option<AggregateTypeList>,
    type_moving_state: Option<UsedType>,
    type_stype: AggregateType,
    const_ordered_set: bool,
    const_parallel: Option<syn::Expr>,
    const_finalize_modify: Option<syn::Expr>,
    const_moving_finalize_modify: Option<syn::Expr>,
    const_initial_condition: Option<String>,
    const_sort_operator: Option<String>,
    const_moving_intial_condition: Option<String>,
    fn_state: Ident,
    fn_finalize: Option<Ident>,
    fn_combine: Option<Ident>,
    fn_serial: Option<Ident>,
    fn_deserial: Option<Ident>,
    fn_moving_state: Option<Ident>,
    fn_moving_state_inverse: Option<Ident>,
    fn_moving_finalize: Option<Ident>,
    hypothetical: bool,
    to_sql_config: ToSqlConfig,
}

impl PgAggregate {
    pub fn new(mut item_impl: ItemImpl) -> Result<CodeEnrichment<Self>, syn::Error> {
        let to_sql_config =
            ToSqlConfig::from_attributes(item_impl.attrs.as_slice())?.unwrap_or_default();
        let target_path = get_target_path(&item_impl)?;
        let target_ident = get_target_ident(&target_path)?;

        let snake_case_target_ident =
            Ident::new(&target_ident.to_string().to_case(Case::Snake), target_ident.span());
        crate::ident_is_acceptable_to_postgres(&snake_case_target_ident)?;

        let mut pg_externs = Vec::default();
        // We want to avoid having multiple borrows, so we take a snapshot to scan from,
        // and mutate the actual one.
        let item_impl_snapshot = item_impl.clone();

        if let Some((_, ref path, _)) = item_impl.trait_ {
            // TODO: Consider checking the path if there is more than one segment to make sure it's pgrx.
            if let Some(last) = path.segments.last() {
                if last.ident != "Aggregate" {
                    return Err(syn::Error::new(
                        last.ident.span(),
                        "`#[pg_aggregate]` only works with the `Aggregate` trait.",
                    ));
                }
            }
        }

        let name = match get_impl_const_by_name(&item_impl_snapshot, "NAME") {
            Some(item_const) => match &item_const.expr {
                syn::Expr::Lit(ref expr) => {
                    if let syn::Lit::Str(_) = &expr.lit {
                        item_const.expr.clone()
                    } else {
                        return Err(syn::Error::new(
                            expr.span(),
                            "`NAME` must be a `&'static str` for Aggregate implementations.",
                        ));
                    }
                }
                e => {
                    return Err(syn::Error::new(
                        e.span(),
                        "`NAME` must be a `&'static str` for Aggregate implementations.",
                    ));
                }
            },
            None => {
                item_impl.items.push(parse_quote! {
                    const NAME: &'static str = stringify!(Self);
                });
                parse_quote! {
                    stringify!(#target_ident)
                }
            }
        };

        // `State` is an optional value, we default to `Self`.
        let type_state = get_impl_type_by_name(&item_impl_snapshot, "State");
        let _type_state_value = type_state.map(|v| v.ty.clone());

        let type_state_without_self = if let Some(inner) = type_state {
            let mut remapped = inner.ty.clone();
            remap_self_to_target(&mut remapped, &target_ident);
            remapped
        } else {
            item_impl.items.push(parse_quote! {
                type State = Self;
            });
            let mut remapped = parse_quote!(Self);
            remap_self_to_target(&mut remapped, &target_ident);
            remapped
        };
        let type_stype = AggregateType {
            used_ty: UsedType::new(type_state_without_self.clone())?,
            name: Some("state".into()),
        };

        // `MovingState` is an optional value, we default to nothing.
        let impl_type_moving_state = get_impl_type_by_name(&item_impl_snapshot, "MovingState");
        let type_moving_state;
        let type_moving_state_value = if let Some(impl_type_moving_state) = impl_type_moving_state {
            type_moving_state = impl_type_moving_state.ty.clone();
            Some(UsedType::new(type_moving_state.clone())?)
        } else {
            item_impl.items.push(parse_quote! {
                type MovingState = ();
            });
            type_moving_state = parse_quote! { () };
            None
        };

        // `OrderBy` is an optional value, we default to nothing.
        let type_ordered_set_args = get_impl_type_by_name(&item_impl_snapshot, "OrderedSetArgs");
        let type_ordered_set_args_value =
            type_ordered_set_args.map(|v| AggregateTypeList::new(v.ty.clone())).transpose()?;
        if type_ordered_set_args.is_none() {
            item_impl.items.push(parse_quote! {
                type OrderedSetArgs = ();
            })
        }
        let (direct_args_with_names, direct_arg_names) = if let Some(ref order_by_direct_args) =
            type_ordered_set_args_value
        {
            let direct_args = order_by_direct_args
                .found
                .iter()
                .map(|x| {
                    (x.name.clone(), x.used_ty.resolved_ty.clone(), x.used_ty.original_ty.clone())
                })
                .collect::<Vec<_>>();
            let direct_arg_names = ARG_NAMES[0..direct_args.len()]
                .iter()
                .zip(direct_args.iter())
                .map(|(default_name, (custom_name, _ty, _orig))| {
                    Ident::new(
                        &custom_name.clone().unwrap_or_else(|| default_name.to_string()),
                        Span::mixed_site(),
                    )
                })
                .collect::<Vec<_>>();
            let direct_args_with_names = direct_args
                .iter()
                .zip(direct_arg_names.iter())
                .map(|(arg, name)| {
                    let arg_ty = &arg.2; // original_type
                    parse_quote! {
                        #name: #arg_ty
                    }
                })
                .collect::<Vec<syn::FnArg>>();
            (direct_args_with_names, direct_arg_names)
        } else {
            (Vec::default(), Vec::default())
        };

        // `Args` is an optional value, we default to nothing.
        let type_args = get_impl_type_by_name(&item_impl_snapshot, "Args").ok_or_else(|| {
            syn::Error::new(
                item_impl_snapshot.span(),
                "`#[pg_aggregate]` requires the `Args` type defined.",
            )
        })?;
        let type_args_value = AggregateTypeList::new(type_args.ty.clone())?;
        let args = type_args_value
            .found
            .iter()
            .map(|x| (x.name.clone(), x.used_ty.original_ty.clone()))
            .collect::<Vec<_>>();
        let arg_names = ARG_NAMES[0..args.len()]
            .iter()
            .zip(args.iter())
            .map(|(default_name, (custom_name, ty))| {
                Ident::new(
                    &custom_name.clone().unwrap_or_else(|| default_name.to_string()),
                    ty.span(),
                )
            })
            .collect::<Vec<_>>();
        let args_with_names = args
            .iter()
            .zip(arg_names.iter())
            .map(|(arg, name)| {
                let arg_ty = &arg.1;
                quote! {
                    #name: #arg_ty
                }
            })
            .collect::<Vec<_>>();

        // `Finalize` is an optional value, we default to nothing.
        let impl_type_finalize = get_impl_type_by_name(&item_impl_snapshot, "Finalize");
        let type_finalize: syn::Type = if let Some(type_finalize) = impl_type_finalize {
            type_finalize.ty.clone()
        } else {
            item_impl.items.push(parse_quote! {
                type Finalize = ();
            });
            parse_quote! { () }
        };

        let fn_state = get_impl_func_by_name(&item_impl_snapshot, "state");

        let fn_state_name = if let Some(found) = fn_state {
            let fn_name =
                Ident::new(&format!("{}_state", snake_case_target_ident), found.sig.ident.span());
            let pg_extern_attr = pg_extern_attr(found);

            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(this: #type_state_without_self, #(#args_with_names),*, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_state_without_self {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::state(this, (#(#arg_names),*), fcinfo)
                        )
                    }
                }
            });
            fn_name
        } else {
            return Err(syn::Error::new(
                item_impl.span(),
                "Aggregate implementation must include state function.",
            ));
        };

        let fn_combine = get_impl_func_by_name(&item_impl_snapshot, "combine");
        let fn_combine_name = if let Some(found) = fn_combine {
            let fn_name =
                Ident::new(&format!("{}_combine", snake_case_target_ident), found.sig.ident.span());
            let pg_extern_attr = pg_extern_attr(found);
            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(this: #type_state_without_self, v: #type_state_without_self, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_state_without_self {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::combine(this, v, fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn combine(current: #type_state_without_self, _other: #type_state_without_self, _fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_state_without_self {
                    unimplemented!("Call to combine on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_finalize = get_impl_func_by_name(&item_impl_snapshot, "finalize");
        let fn_finalize_name = if let Some(found) = fn_finalize {
            let fn_name = Ident::new(
                &format!("{}_finalize", snake_case_target_ident),
                found.sig.ident.span(),
            );
            let pg_extern_attr = pg_extern_attr(found);

            if !direct_args_with_names.is_empty() {
                pg_externs.push(parse_quote! {
                    #[allow(non_snake_case, clippy::too_many_arguments)]
                    #pg_extern_attr
                    fn #fn_name(this: #type_state_without_self, #(#direct_args_with_names),*, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_finalize {
                        unsafe {
                            <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                                fcinfo,
                                move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::finalize(this, (#(#direct_arg_names),*), fcinfo)
                            )
                        }
                    }
                });
            } else {
                pg_externs.push(parse_quote! {
                    #[allow(non_snake_case, clippy::too_many_arguments)]
                    #pg_extern_attr
                    fn #fn_name(this: #type_state_without_self, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_finalize {
                        unsafe {
                            <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                                fcinfo,
                                move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::finalize(this, (), fcinfo)
                            )
                        }
                    }
                });
            };
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn finalize(current: Self::State, direct_args: Self::OrderedSetArgs, _fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_finalize {
                    unimplemented!("Call to finalize on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_serial = get_impl_func_by_name(&item_impl_snapshot, "serial");
        let fn_serial_name = if let Some(found) = fn_serial {
            let fn_name =
                Ident::new(&format!("{}_serial", snake_case_target_ident), found.sig.ident.span());
            let pg_extern_attr = pg_extern_attr(found);
            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(this: #type_state_without_self, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> Vec<u8> {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::serial(this, fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn serial(current: #type_state_without_self, _fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> Vec<u8> {
                    unimplemented!("Call to serial on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_deserial = get_impl_func_by_name(&item_impl_snapshot, "deserial");
        let fn_deserial_name = if let Some(found) = fn_deserial {
            let fn_name = Ident::new(
                &format!("{}_deserial", snake_case_target_ident),
                found.sig.ident.span(),
            );
            let pg_extern_attr = pg_extern_attr(found);
            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(this: #type_state_without_self, buf: Vec<u8>, internal: ::pgrx::pgbox::PgBox<#type_state_without_self>, fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> ::pgrx::pgbox::PgBox<#type_state_without_self> {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::deserial(this, buf, internal, fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn deserial(current: #type_state_without_self, _buf: Vec<u8>, _internal: ::pgrx::pgbox::PgBox<#type_state_without_self>, _fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> ::pgrx::pgbox::PgBox<#type_state_without_self> {
                    unimplemented!("Call to deserial on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_moving_state = get_impl_func_by_name(&item_impl_snapshot, "moving_state");
        let fn_moving_state_name = if let Some(found) = fn_moving_state {
            let fn_name = Ident::new(
                &format!("{}_moving_state", snake_case_target_ident),
                found.sig.ident.span(),
            );
            let pg_extern_attr = pg_extern_attr(found);

            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(
                    mstate: #type_moving_state,
                    #(#args_with_names),*,
                    fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
                ) -> #type_moving_state {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::moving_state(mstate, (#(#arg_names),*), fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn moving_state(
                    _mstate: <#target_path as ::pgrx::aggregate::Aggregate>::MovingState,
                    _v: Self::Args,
                    _fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
                ) -> <#target_path as ::pgrx::aggregate::Aggregate>::MovingState {
                    unimplemented!("Call to moving_state on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_moving_state_inverse =
            get_impl_func_by_name(&item_impl_snapshot, "moving_state_inverse");
        let fn_moving_state_inverse_name = if let Some(found) = fn_moving_state_inverse {
            let fn_name = Ident::new(
                &format!("{}_moving_state_inverse", snake_case_target_ident),
                found.sig.ident.span(),
            );
            let pg_extern_attr = pg_extern_attr(found);
            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(
                    mstate: #type_moving_state,
                    #(#args_with_names),*,
                    fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
                ) -> #type_moving_state {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::moving_state_inverse(mstate, (#(#arg_names),*), fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn moving_state_inverse(
                    _mstate: #type_moving_state,
                    _v: Self::Args,
                    _fcinfo: ::pgrx::pg_sys::FunctionCallInfo,
                ) -> #type_moving_state {
                    unimplemented!("Call to moving_state on an aggregate which does not support it.")
                }
            });
            None
        };

        let fn_moving_finalize = get_impl_func_by_name(&item_impl_snapshot, "moving_finalize");
        let fn_moving_finalize_name = if let Some(found) = fn_moving_finalize {
            let fn_name = Ident::new(
                &format!("{}_moving_finalize", snake_case_target_ident),
                found.sig.ident.span(),
            );
            let pg_extern_attr = pg_extern_attr(found);
            let maybe_comma: Option<syn::Token![,]> =
                if !direct_args_with_names.is_empty() { Some(parse_quote! {,}) } else { None };

            pg_externs.push(parse_quote! {
                #[allow(non_snake_case, clippy::too_many_arguments)]
                #pg_extern_attr
                fn #fn_name(mstate: #type_moving_state, #(#direct_args_with_names),* #maybe_comma fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> #type_finalize {
                    unsafe {
                        <#target_path as ::pgrx::aggregate::Aggregate>::in_memory_context(
                            fcinfo,
                            move |_context| <#target_path as ::pgrx::aggregate::Aggregate>::moving_finalize(mstate, (#(#direct_arg_names),*), fcinfo)
                        )
                    }
                }
            });
            Some(fn_name)
        } else {
            item_impl.items.push(parse_quote! {
                fn moving_finalize(_mstate: Self::MovingState, direct_args: Self::OrderedSetArgs, _fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> Self::Finalize {
                    unimplemented!("Call to moving_finalize on an aggregate which does not support it.")
                }
            });
            None
        };

        Ok(CodeEnrichment(Self {
            item_impl,
            target_ident,
            pg_externs,
            name,
            type_args: type_args_value,
            type_ordered_set_args: type_ordered_set_args_value,
            type_moving_state: type_moving_state_value,
            type_stype,
            const_parallel: get_impl_const_by_name(&item_impl_snapshot, "PARALLEL")
                .map(|x| x.expr.clone()),
            const_finalize_modify: get_impl_const_by_name(&item_impl_snapshot, "FINALIZE_MODIFY")
                .map(|x| x.expr.clone()),
            const_moving_finalize_modify: get_impl_const_by_name(
                &item_impl_snapshot,
                "MOVING_FINALIZE_MODIFY",
            )
            .map(|x| x.expr.clone()),
            const_initial_condition: get_impl_const_by_name(
                &item_impl_snapshot,
                "INITIAL_CONDITION",
            )
            .and_then(|e| get_const_litstr(e).transpose())
            .transpose()?,
            const_ordered_set: get_impl_const_by_name(&item_impl_snapshot, "ORDERED_SET")
                .and_then(get_const_litbool)
                .unwrap_or(false),
            const_sort_operator: get_impl_const_by_name(&item_impl_snapshot, "SORT_OPERATOR")
                .and_then(|e| get_const_litstr(e).transpose())
                .transpose()?,
            const_moving_intial_condition: get_impl_const_by_name(
                &item_impl_snapshot,
                "MOVING_INITIAL_CONDITION",
            )
            .and_then(|e| get_const_litstr(e).transpose())
            .transpose()?,
            fn_state: fn_state_name,
            fn_finalize: fn_finalize_name,
            fn_combine: fn_combine_name,
            fn_serial: fn_serial_name,
            fn_deserial: fn_deserial_name,
            fn_moving_state: fn_moving_state_name,
            fn_moving_state_inverse: fn_moving_state_inverse_name,
            fn_moving_finalize: fn_moving_finalize_name,
            hypothetical: if let Some(value) =
                get_impl_const_by_name(&item_impl_snapshot, "HYPOTHETICAL")
            {
                match &value.expr {
                    syn::Expr::Lit(expr_lit) => match &expr_lit.lit {
                        syn::Lit::Bool(lit) => lit.value,
                        _ => return Err(syn::Error::new(value.span(), "`#[pg_aggregate]` required the `HYPOTHETICAL` value to be a literal boolean.")),
                    },
                    _ => return Err(syn::Error::new(value.span(), "`#[pg_aggregate]` required the `HYPOTHETICAL` value to be a literal boolean.")),
                }
            } else {
                false
            },
            to_sql_config,
        }))
    }
}

impl ToEntityGraphTokens for PgAggregate {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let target_ident = &self.target_ident;
        let snake_case_target_ident =
            Ident::new(&target_ident.to_string().to_case(Case::Snake), target_ident.span());
        let sql_graph_entity_fn_name = syn::Ident::new(
            &format!("__pgrx_internals_aggregate_{}", snake_case_target_ident),
            target_ident.span(),
        );

        let name = &self.name;
        let type_args_iter = &self.type_args.entity_tokens();
        let type_order_by_args_iter = self.type_ordered_set_args.iter().map(|x| x.entity_tokens());

        let type_moving_state_entity_tokens =
            self.type_moving_state.clone().map(|v| v.entity_tokens());
        let type_moving_state_entity_tokens_iter = type_moving_state_entity_tokens.iter();
        let type_stype = self.type_stype.entity_tokens();
        let const_ordered_set = self.const_ordered_set;
        let const_parallel_iter = self.const_parallel.iter();
        let const_finalize_modify_iter = self.const_finalize_modify.iter();
        let const_moving_finalize_modify_iter = self.const_moving_finalize_modify.iter();
        let const_initial_condition_iter = self.const_initial_condition.iter();
        let const_sort_operator_iter = self.const_sort_operator.iter();
        let const_moving_intial_condition_iter = self.const_moving_intial_condition.iter();
        let hypothetical = self.hypothetical;
        let fn_state = &self.fn_state;
        let fn_finalize_iter = self.fn_finalize.iter();
        let fn_combine_iter = self.fn_combine.iter();
        let fn_serial_iter = self.fn_serial.iter();
        let fn_deserial_iter = self.fn_deserial.iter();
        let fn_moving_state_iter = self.fn_moving_state.iter();
        let fn_moving_state_inverse_iter = self.fn_moving_state_inverse.iter();
        let fn_moving_finalize_iter = self.fn_moving_finalize.iter();
        let to_sql_config = &self.to_sql_config;

        quote! {
            #[no_mangle]
            #[doc(hidden)]
            #[allow(unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                let submission = ::pgrx::pgrx_sql_entity_graph::PgAggregateEntity {
                    full_path: ::core::any::type_name::<#target_ident>(),
                    module_path: module_path!(),
                    file: file!(),
                    line: line!(),
                    name: #name,
                    ordered_set: #const_ordered_set,
                    ty_id: ::core::any::TypeId::of::<#target_ident>(),
                    args: #type_args_iter,
                    direct_args: None #( .unwrap_or(Some(#type_order_by_args_iter)) )*,
                    stype: #type_stype,
                    sfunc: stringify!(#fn_state),
                    combinefunc: None #( .unwrap_or(Some(stringify!(#fn_combine_iter))) )*,
                    finalfunc: None #( .unwrap_or(Some(stringify!(#fn_finalize_iter))) )*,
                    finalfunc_modify: None #( .unwrap_or(#const_finalize_modify_iter) )*,
                    initcond: None #( .unwrap_or(Some(#const_initial_condition_iter)) )*,
                    serialfunc: None #( .unwrap_or(Some(stringify!(#fn_serial_iter))) )*,
                    deserialfunc: None #( .unwrap_or(Some(stringify!(#fn_deserial_iter))) )*,
                    msfunc: None #( .unwrap_or(Some(stringify!(#fn_moving_state_iter))) )*,
                    minvfunc: None #( .unwrap_or(Some(stringify!(#fn_moving_state_inverse_iter))) )*,
                    mstype: None #( .unwrap_or(Some(#type_moving_state_entity_tokens_iter)) )*,
                    mfinalfunc: None #( .unwrap_or(Some(stringify!(#fn_moving_finalize_iter))) )*,
                    mfinalfunc_modify: None #( .unwrap_or(#const_moving_finalize_modify_iter) )*,
                    minitcond: None #( .unwrap_or(Some(#const_moving_intial_condition_iter)) )*,
                    sortop: None #( .unwrap_or(Some(#const_sort_operator_iter)) )*,
                    parallel: None #( .unwrap_or(#const_parallel_iter) )*,
                    hypothetical: #hypothetical,
                    to_sql_config: #to_sql_config,
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::Aggregate(submission)
            }
        }
    }
}

impl ToRustCodeTokens for PgAggregate {
    fn to_rust_code_tokens(&self) -> TokenStream2 {
        let impl_item = &self.item_impl;
        let pg_externs = self.pg_externs.iter();
        quote! {
            #impl_item
            #(#pg_externs)*
        }
    }
}

impl Parse for CodeEnrichment<PgAggregate> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        PgAggregate::new(input.parse()?)
    }
}

fn get_target_ident(path: &Path) -> Result<Ident, syn::Error> {
    let last = path.segments.last().ok_or_else(|| {
        syn::Error::new(
            path.span(),
            "`#[pg_aggregate]` only works with types whose path have a final segment.",
        )
    })?;
    Ok(last.ident.clone())
}

fn get_target_path(item_impl: &ItemImpl) -> Result<Path, syn::Error> {
    let target_ident = match &*item_impl.self_ty {
        syn::Type::Path(ref type_path) => {
            let last_segment = type_path.path.segments.last().ok_or_else(|| {
                syn::Error::new(
                    type_path.span(),
                    "`#[pg_aggregate]` only works with types whose path have a final segment.",
                )
            })?;
            if last_segment.ident == "PgVarlena" {
                match &last_segment.arguments {
                    syn::PathArguments::AngleBracketed(angled) => {
                        let first = angled.args.first().ok_or_else(|| syn::Error::new(
                            type_path.span(),
                            "`#[pg_aggregate]` only works with `PgVarlena` declarations if they have a type contained.",
                        ))?;
                        match &first {
                            syn::GenericArgument::Type(Type::Path(ty_path)) => ty_path.path.clone(),
                            _ => return Err(syn::Error::new(
                                type_path.span(),
                                "`#[pg_aggregate]` only works with `PgVarlena` declarations if they have a type path contained.",
                            )),
                        }
                    },
                    _ => return Err(syn::Error::new(
                        type_path.span(),
                        "`#[pg_aggregate]` only works with `PgVarlena` declarations if they have a type contained.",
                    )),
                }
            } else {
                type_path.path.clone()
            }
        }
        something_else => {
            return Err(syn::Error::new(
                something_else.span(),
                "`#[pg_aggregate]` only works with types.",
            ))
        }
    };
    Ok(target_ident)
}

fn pg_extern_attr(item: &ImplItemFn) -> syn::Attribute {
    let mut found = None;
    for attr in item.attrs.iter() {
        match attr.path().segments.last() {
            Some(segment) if segment.ident == "pgrx" => {
                found = Some(attr);
                break;
            }
            _ => (),
        };
    }

    let attrs = if let Some(attr) = found {
        let parser = Punctuated::<super::pg_extern::Attribute, syn::Token![,]>::parse_terminated;
        let attrs = attr.parse_args_with(parser);
        attrs.ok()
    } else {
        None
    };

    match attrs {
        Some(args) => parse_quote! {
            #[::pgrx::pg_extern(#args)]
        },
        None => parse_quote! {
            #[::pgrx::pg_extern]
        },
    }
}

fn get_impl_type_by_name<'a>(item_impl: &'a ItemImpl, name: &str) -> Option<&'a ImplItemType> {
    let mut needle = None;
    for impl_item_type in item_impl.items.iter().filter_map(|impl_item| match impl_item {
        syn::ImplItem::Type(iitype) => Some(iitype),
        _ => None,
    }) {
        let ident_string = impl_item_type.ident.to_string();
        if ident_string == name {
            needle = Some(impl_item_type);
        }
    }
    needle
}

fn get_impl_func_by_name<'a>(item_impl: &'a ItemImpl, name: &str) -> Option<&'a ImplItemFn> {
    let mut needle = None;
    for impl_item_fn in item_impl.items.iter().filter_map(|impl_item| match impl_item {
        syn::ImplItem::Fn(iifn) => Some(iifn),
        _ => None,
    }) {
        let ident_string = impl_item_fn.sig.ident.to_string();
        if ident_string == name {
            needle = Some(impl_item_fn);
        }
    }
    needle
}

fn get_impl_const_by_name<'a>(item_impl: &'a ItemImpl, name: &str) -> Option<&'a ImplItemConst> {
    let mut needle = None;
    for impl_item_const in item_impl.items.iter().filter_map(|impl_item| match impl_item {
        syn::ImplItem::Const(iiconst) => Some(iiconst),
        _ => None,
    }) {
        let ident_string = impl_item_const.ident.to_string();
        if ident_string == name {
            needle = Some(impl_item_const);
        }
    }
    needle
}

fn get_const_litbool(item: &ImplItemConst) -> Option<bool> {
    match &item.expr {
        syn::Expr::Lit(expr_lit) => match &expr_lit.lit {
            syn::Lit::Bool(lit) => Some(lit.value()),
            _ => None,
        },
        _ => None,
    }
}

fn get_const_litstr(item: &ImplItemConst) -> syn::Result<Option<String>> {
    match &item.expr {
        syn::Expr::Lit(expr_lit) => match &expr_lit.lit {
            syn::Lit::Str(lit) => Ok(Some(lit.value())),
            _ => Ok(None),
        },
        syn::Expr::Call(expr_call) => match &*expr_call.func {
            syn::Expr::Path(expr_path) => {
                let Some(last) = expr_path.path.segments.last() else {
                    return Ok(None);
                };
                if last.ident == "Some" {
                    match expr_call.args.first() {
                        Some(syn::Expr::Lit(expr_lit)) => match &expr_lit.lit {
                            syn::Lit::Str(lit) => Ok(Some(lit.value())),
                            _ => Ok(None),
                        },
                        _ => Ok(None),
                    }
                } else {
                    Ok(None)
                }
            }
            _ => Ok(None),
        },
        ex => Err(syn::Error::new(ex.span(), "")),
    }
}

fn remap_self_to_target(ty: &mut syn::Type, target: &syn::Ident) {
    if let Type::Path(ref mut ty_path) = ty {
        for segment in ty_path.path.segments.iter_mut() {
            if segment.ident == "Self" {
                segment.ident = target.clone()
            }
            use syn::{GenericArgument, PathArguments};
            match segment.arguments {
                PathArguments::AngleBracketed(ref mut angle_args) => {
                    for arg in angle_args.args.iter_mut() {
                        if let GenericArgument::Type(inner_ty) = arg {
                            remap_self_to_target(inner_ty, target)
                        }
                    }
                }
                PathArguments::Parenthesized(_) => (),
                PathArguments::None => (),
            }
        }
    }
}

fn get_pgrx_attr_macro(attr_name: impl AsRef<str>, ty: &syn::Type) -> Option<TokenStream2> {
    match &ty {
        syn::Type::Macro(ty_macro) => {
            let mut found_pgrx = false;
            let mut found_attr = false;
            // We don't actually have type resolution here, this is a "Best guess".
            for (idx, segment) in ty_macro.mac.path.segments.iter().enumerate() {
                match segment.ident.to_string().as_str() {
                    "pgrx" if idx == 0 => found_pgrx = true,
                    attr if attr == attr_name.as_ref() => found_attr = true,
                    _ => (),
                }
            }
            if (found_pgrx || ty_macro.mac.path.segments.len() == 1) && found_attr {
                Some(ty_macro.mac.tokens.clone())
            } else {
                None
            }
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::PgAggregate;
    use eyre::Result;
    use quote::ToTokens;
    use syn::{parse_quote, ItemImpl};

    #[test]
    fn agg_required_only() -> Result<()> {
        let tokens: ItemImpl = parse_quote! {
            #[pg_aggregate]
            impl Aggregate for DemoAgg {
                type State = PgVarlena<Self>;
                type Args = i32;
                const NAME: &'static str = "DEMO";

                fn state(mut current: Self::State, arg: Self::Args) -> Self::State {
                    todo!()
                }
            }
        };
        // It should not error, as it's valid.
        let agg = PgAggregate::new(tokens);
        assert!(agg.is_ok());
        // It should create 1 extern, the state.
        let agg = agg.unwrap();
        assert_eq!(agg.0.pg_externs.len(), 1);
        // That extern should be named specifically:
        let extern_fn = &agg.0.pg_externs[0];
        assert_eq!(extern_fn.sig.ident.to_string(), "demo_agg_state");
        // It should be possible to generate entity tokens.
        let _ = agg.to_token_stream();
        Ok(())
    }

    #[test]
    fn agg_all_options() -> Result<()> {
        let tokens: ItemImpl = parse_quote! {
            #[pg_aggregate]
            impl Aggregate for DemoAgg {
                type State = PgVarlena<Self>;
                type Args = i32;
                type OrderBy = i32;
                type MovingState = i32;

                const NAME: &'static str = "DEMO";

                const PARALLEL: Option<ParallelOption> = Some(ParallelOption::Safe);
                const FINALIZE_MODIFY: Option<FinalizeModify> = Some(FinalizeModify::ReadWrite);
                const MOVING_FINALIZE_MODIFY: Option<FinalizeModify> = Some(FinalizeModify::ReadWrite);
                const SORT_OPERATOR: Option<&'static str> = Some("sortop");
                const MOVING_INITIAL_CONDITION: Option<&'static str> = Some("1,1");
                const HYPOTHETICAL: bool = true;

                fn state(current: Self::State, v: Self::Args) -> Self::State {
                    todo!()
                }

                fn finalize(current: Self::State) -> Self::Finalize {
                    todo!()
                }

                fn combine(current: Self::State, _other: Self::State) -> Self::State {
                    todo!()
                }

                fn serial(current: Self::State) -> Vec<u8> {
                    todo!()
                }

                fn deserial(current: Self::State, _buf: Vec<u8>, _internal: PgBox<Self>) -> PgBox<Self> {
                    todo!()
                }

                fn moving_state(_mstate: Self::MovingState, _v: Self::Args) -> Self::MovingState {
                    todo!()
                }

                fn moving_state_inverse(_mstate: Self::MovingState, _v: Self::Args) -> Self::MovingState {
                    todo!()
                }

                fn moving_finalize(_mstate: Self::MovingState) -> Self::Finalize {
                    todo!()
                }
            }
        };
        // It should not error, as it's valid.
        let agg = PgAggregate::new(tokens);
        assert!(agg.is_ok());
        // It should create 8 externs!
        let agg = agg.unwrap();
        assert_eq!(agg.0.pg_externs.len(), 8);
        // That extern should be named specifically:
        let extern_fn = &agg.0.pg_externs[0];
        assert_eq!(extern_fn.sig.ident.to_string(), "demo_agg_state");
        // It should be possible to generate entity tokens.
        let _ = agg.to_token_stream();
        Ok(())
    }

    #[test]
    fn agg_missing_required() -> Result<()> {
        // This is not valid as it is missing required types/consts.
        let tokens: ItemImpl = parse_quote! {
            #[pg_aggregate]
            impl Aggregate for IntegerAvgState {
            }
        };
        let agg = PgAggregate::new(tokens);
        assert!(agg.is_err());
        Ok(())
    }
}
