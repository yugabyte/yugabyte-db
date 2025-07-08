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

Type level metadata for Rust to SQL generation.

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::composite_type::{handle_composite_type_macro, CompositeTypeMacro};
use crate::lifetimes::anonymize_lifetimes;

use quote::ToTokens;
use syn::parse::{Parse, ParseStream};
use syn::spanned::Spanned;
use syn::{GenericArgument, Token};

use super::metadata::FunctionMetadataTypeEntity;

/// A type, optionally with an overriding composite type name
#[derive(Debug, Clone)]
pub struct UsedType {
    pub original_ty: syn::Type,
    pub resolved_ty: syn::Type,
    pub resolved_ty_inner: Option<syn::Type>,
    /// Set via `composite_type!()`
    pub composite_type: Option<CompositeTypeMacro>,
    /// Set via `VariadicArray` or `variadic!()`
    pub variadic: bool,
    pub default: Option<String>,
    /// Set via the type being an `Option` or a `Result<Option<T>>`.
    pub optional: Option<syn::Type>,
    /// Set via the type being a `Result<T>`
    pub result: bool,
}

impl UsedType {
    pub fn new(ty: syn::Type) -> syn::Result<Self> {
        let original_ty = ty.clone();
        // There are several steps:
        // * Resolve the `default!()` macro
        // * Resolve the `variadic!()` macro
        // * Resolve `composite_type!()`
        // * Anonymize any lifetimes
        // * Resolving any flags for that resolved type so we can not have to do this later.

        // Resolve any `default` macro
        // We do this first as it's **always** in the first position. It's not valid deeper in the type.
        let (resolved_ty, default) = match ty.clone() {
            // default!(..)
            syn::Type::Macro(macro_pat) => {
                let mac = &macro_pat.mac;
                let archetype = mac.path.segments.last().expect("No last segment");
                match archetype.ident.to_string().as_str() {
                    "default" => {
                        let (maybe_resolved_ty, default) = handle_default_macro(mac)?;
                        (maybe_resolved_ty, default)
                    }
                    _ => (syn::Type::Macro(macro_pat), None),
                }
            }
            original => (original, None),
        };

        // Now, resolve any `composite_type` macro
        let (resolved_ty, composite_type) = match resolved_ty {
            // composite_type!(..)
            syn::Type::Macro(macro_pat) => {
                let mac = &macro_pat.mac;
                match &*mac.path.segments.last().expect("No last segment").ident.to_string() {
                    "default" => {
                        // If we land here, after already expanding the `default!()` above, the user has written it twice.
                        // This is definitely an issue and we should tell them.
                        return Err(syn::Error::new(
                            mac.span(),
                            "default!(default!()) not supported, use it only once",
                        ))?;
                    }
                    "composite_type" => {
                        let composite_macro = handle_composite_type_macro(mac)?;
                        let ty = composite_macro.expand_with_lifetime();
                        (ty, Some(composite_macro))
                    }
                    _ => (syn::Type::Macro(macro_pat), None),
                }
            }
            syn::Type::Path(path) => {
                let segments = &path.path;
                let last = segments
                    .segments
                    .last()
                    .ok_or(syn::Error::new(path.span(), "Could not read last segment of path"))?;

                match last.ident.to_string().as_str() {
                    // Option<composite_type!(..)>
                    // Option<Vec<composite_type!(..)>>
                    // Option<Vec<Option<composite_type!(..)>>>
                    // Option<VariadicArray<composite_type!(..)>>
                    // Option<VariadicArray<Option<composite_type!(..)>>>
                    "Option" => resolve_option_inner(path)?,
                    // Result<composite_type!(..), ..>
                    // Result<Vec<composite_type!(..)>, ..>
                    // Result<Vec<Option<composite_type!(..)>>, ..>
                    // Result<VariadicArray<composite_type!(..)>, ..>
                    // Result<VariadicArray<Option<composite_type!(..)>>, ..>
                    "Result" => resolve_result_inner(path)?,
                    // Vec<composite_type!(..)>
                    // Vec<Option<composite_type!(..)>>
                    "Vec" => resolve_vec_inner(path)?,
                    // VariadicArray<composite_type!(..)>
                    // VariadicArray<Option<composite_type!(..)>>
                    "VariadicArray" => resolve_variadic_array_inner(path)?,
                    // Array<composite_type!(..)>
                    // Array<Option<composite_type!(..)>>
                    "Array" => resolve_array_inner(path)?,
                    _ => (syn::Type::Path(path), None),
                }
            }
            original => (original, None),
        };

        // In this  step, we go look at the resolved type and determine if it is a variadic, optional, result, etc.
        let (resolved_ty, variadic, optional, result) = match resolved_ty {
            syn::Type::Path(type_path) => {
                let path = &type_path.path;
                let last_segment = path.segments.last().ok_or(syn::Error::new(
                    path.span(),
                    "No last segment found while scanning path",
                ))?;
                let ident_string = last_segment.ident.to_string();
                match ident_string.as_str() {
                    "Result" => {
                        match &last_segment.arguments {
                            syn::PathArguments::AngleBracketed(angle_bracketed) => {
                                match angle_bracketed.args.first().ok_or(syn::Error::new(
                                    angle_bracketed.span(),
                                    "No inner arg for Result<T, E> found",
                                ))? {
                                    syn::GenericArgument::Type(inner_ty) => {
                                        match inner_ty {
                                            // Result<$Type<T>>
                                            syn::Type::Path(ref inner_type_path) => {
                                                let path = &inner_type_path.path;
                                                let last_segment =
                                                    path.segments.last().ok_or(syn::Error::new(
                                                        path.span(),
                                                        "No last segment found while scanning path",
                                                    ))?;
                                                let ident_string = last_segment.ident.to_string();
                                                match ident_string.as_str() {
                                                    "VariadicArray" => (
                                                        syn::Type::Path(type_path.clone()),
                                                        true,
                                                        Some(inner_ty.clone()),
                                                        false,
                                                    ),
                                                    "Option" => (
                                                        syn::Type::Path(type_path.clone()),
                                                        false,
                                                        Some(inner_ty.clone()),
                                                        true,
                                                    ),
                                                    _ => (
                                                        syn::Type::Path(type_path.clone()),
                                                        false,
                                                        None,
                                                        true,
                                                    ),
                                                }
                                            }
                                            // Result<T>
                                            _ => (
                                                syn::Type::Path(type_path.clone()),
                                                false,
                                                None,
                                                true,
                                            ),
                                        }
                                    }
                                    _ => {
                                        return Err(syn::Error::new(
                                            type_path.span(),
                                            "Unexpected Item found inside `Result` (expected Type)",
                                        ))
                                    }
                                }
                            }
                            _ => return Err(syn::Error::new(
                                type_path.span(),
                                "Unexpected Item found inside `Result` (expected Angle Brackets)",
                            )),
                        }
                    }
                    "Option" => {
                        // Option<VariadicArray<T>>
                        match &last_segment.arguments {
                            syn::PathArguments::AngleBracketed(angle_bracketed) => {
                                match angle_bracketed.args.first().ok_or(syn::Error::new(
                                    angle_bracketed.span(),
                                    "No inner arg for Option<T> found",
                                ))? {
                                    syn::GenericArgument::Type(inner_ty) => {
                                        match inner_ty {
                                            // Option<VariadicArray<T>>
                                            syn::Type::Path(ref inner_type_path) => {
                                                let path = &inner_type_path.path;
                                                let last_segment =
                                                    path.segments.last().ok_or(syn::Error::new(
                                                        path.span(),
                                                        "No last segment found while scanning path",
                                                    ))?;
                                                let ident_string = last_segment.ident.to_string();
                                                match ident_string.as_str() {
                                                    // Option<VariadicArray<T>>
                                                    "VariadicArray" => (
                                                        syn::Type::Path(type_path.clone()),
                                                        true,
                                                        Some(inner_ty.clone()),
                                                        false,
                                                    ),
                                                    _ => (
                                                        syn::Type::Path(type_path.clone()),
                                                        false,
                                                        Some(inner_ty.clone()),
                                                        false,
                                                    ),
                                                }
                                            }
                                            // Option<T>
                                            _ => (
                                                syn::Type::Path(type_path.clone()),
                                                false,
                                                Some(inner_ty.clone()),
                                                false,
                                            ),
                                        }
                                    }
                                    // Option<T>
                                    _ => {
                                        return Err(syn::Error::new(
                                            type_path.span(),
                                            "Unexpected Item found inside `Option` (expected Type)",
                                        ))
                                    }
                                }
                            }
                            // Option<T>
                            _ => return Err(syn::Error::new(
                                type_path.span(),
                                "Unexpected Item found inside `Option` (expected Angle Brackets)",
                            )),
                        }
                    }
                    // VariadicArray<T>
                    "VariadicArray" => (syn::Type::Path(type_path), true, None, false),
                    // T
                    _ => (syn::Type::Path(type_path), false, None, false),
                }
            }
            original => (original, false, None, false),
        };

        // if the Type is like `Result<T, E>`, this finds the `T`
        let mut resolved_ty_inner: Option<syn::Type> = None;
        if result {
            if let syn::Type::Path(tp) = &resolved_ty {
                if let Some(first_segment) = tp.path.segments.first() {
                    if let syn::PathArguments::AngleBracketed(ab) = &first_segment.arguments {
                        if let Some(syn::GenericArgument::Type(ty)) = ab.args.first() {
                            resolved_ty_inner = Some(ty.clone());
                        }
                    }
                }
            }
        }

        Ok(Self {
            original_ty,
            resolved_ty,
            resolved_ty_inner,
            optional,
            result,
            variadic,
            default,
            composite_type,
        })
    }

    pub fn entity_tokens(&self) -> syn::Expr {
        let mut resolved_ty = self.resolved_ty.clone();
        let mut resolved_ty_inner = self.resolved_ty_inner.clone().unwrap_or(resolved_ty.clone());
        // The lifetimes of these are not relevant. Previously, we solved this by staticizing them
        // but we want to avoid staticizing in this codebase going forward. Anonymization makes it
        // easier to name the lifetime-bounded objects without the context for those lifetimes,
        // without erasing all possible distinctions, since anon lifetimes may still be disunited.
        // Non-static lifetimes, however, require the use of the NonStaticTypeId hack.
        anonymize_lifetimes(&mut resolved_ty);
        anonymize_lifetimes(&mut resolved_ty_inner);
        let resolved_ty_string = resolved_ty.to_token_stream().to_string();
        let composite_type = self.composite_type.clone().map(|v| v.expr);
        let composite_type_iter = composite_type.iter();
        let variadic = &self.variadic;
        let optional = &self.optional.is_some();
        let default = self.default.iter();

        syn::parse_quote! {
            ::pgrx::pgrx_sql_entity_graph::UsedTypeEntity {
                ty_source: #resolved_ty_string,
                ty_id: core::any::TypeId::of::<#resolved_ty_inner>(),
                full_path: core::any::type_name::<#resolved_ty>(),
                module_path: {
                    let ty_name = core::any::type_name::<#resolved_ty>();
                    let mut path_items: Vec<_> = ty_name.split("::").collect();
                    let _ = path_items.pop(); // Drop the one we don't want.
                    path_items.join("::")
                },
                composite_type: None #( .unwrap_or(Some(#composite_type_iter)) )*,
                variadic: #variadic,
                default:  None #( .unwrap_or(Some(#default)) )*,
                /// Set via the type being an `Option`.
                optional: #optional,
                metadata: {
                    use ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable;
                    <#resolved_ty>::entity()
                },
            }
        }
    }
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct UsedTypeEntity {
    pub ty_source: &'static str,
    pub ty_id: core::any::TypeId,
    pub full_path: &'static str,
    pub module_path: String,
    pub composite_type: Option<&'static str>,
    pub variadic: bool,
    pub default: Option<&'static str>,
    /// Set via the type being an `Option`.
    pub optional: bool,
    pub metadata: FunctionMetadataTypeEntity,
}

impl crate::TypeIdentifiable for UsedTypeEntity {
    fn ty_id(&self) -> &core::any::TypeId {
        &self.ty_id
    }
    fn ty_name(&self) -> &str {
        self.full_path
    }
}

fn resolve_vec_inner(
    original: syn::TypePath,
) -> syn::Result<(syn::Type, Option<CompositeTypeMacro>)> {
    let segments = &original.path;
    let last = segments
        .segments
        .last()
        .ok_or(syn::Error::new(original.span(), "Could not read last segment of path"))?;

    match &last.arguments {
        syn::PathArguments::AngleBracketed(path_arg) => match path_arg.args.last() {
            Some(syn::GenericArgument::Type(ty)) => match ty.clone() {
                syn::Type::Macro(macro_pat) => {
                    let mac = &macro_pat.mac;
                    let archetype = mac.path.segments.last().expect("No last segment");
                    match archetype.ident.to_string().as_str() {
                        "default" => {
                            Err(syn::Error::new(mac.span(), "`Vec<default!(T, default)>` not supported, choose `default!(Vec<T>, ident)` instead"))
                        }
                        "composite_type" => {
                            let composite_mac = handle_composite_type_macro(mac)?;
                            let comp_ty = composite_mac.expand_with_lifetime();
                            let sql = Some(composite_mac);
                            let ty = syn::parse_quote! {
                                Vec<#comp_ty>
                            };
                            Ok((ty, sql))
                        }
                        _ => Ok((syn::Type::Path(original), None)),
                    }
                }
                syn::Type::Path(arg_type_path) => {
                    let last = arg_type_path.path.segments.last().ok_or(syn::Error::new(
                        arg_type_path.span(),
                        "No last segment in type path",
                    ))?;
                    if last.ident == "Option" {
                        let (inner_ty, expr) = resolve_option_inner(arg_type_path)?;
                        let wrapped_ty = syn::parse_quote! {
                            Vec<#inner_ty>
                        };
                        Ok((wrapped_ty, expr))
                    } else {
                        Ok((syn::Type::Path(original), None))
                    }
                }
                _ => Ok((syn::Type::Path(original), None)),
            },
            _ => Ok((syn::Type::Path(original), None)),
        },
        _ => Ok((syn::Type::Path(original), None)),
    }
}

fn resolve_variadic_array_inner(
    mut original: syn::TypePath,
) -> syn::Result<(syn::Type, Option<CompositeTypeMacro>)> {
    let original_span = original.span();
    let last = original
        .path
        .segments
        .last_mut()
        .ok_or(syn::Error::new(original_span, "Could not read last segment of path"))?;

    match last.arguments {
        syn::PathArguments::AngleBracketed(ref mut path_arg) => {
            match path_arg.args.last() {
                // TODO: Lifetime????
                Some(syn::GenericArgument::Type(ty)) => match ty.clone() {
                    syn::Type::Macro(macro_pat) => {
                        let mac = &macro_pat.mac;
                        let archetype = mac.path.segments.last().expect("No last segment");
                        match archetype.ident.to_string().as_str() {
                            "default" => {
                                Err(syn::Error::new(mac.span(), "`VariadicArray<default!(T, default)>` not supported, choose `default!(VariadicArray<T>, ident)` instead"))
                            }
                            "composite_type" => {
                                let composite_mac = handle_composite_type_macro(mac)?;
                                let comp_ty = composite_mac.expand_with_lifetime();
                                let sql = Some(composite_mac);
                                let ty = syn::parse_quote! {
                                    ::pgrx::datum::VariadicArray<'_, #comp_ty>
                                };
                                Ok((ty, sql))
                            }
                            _ => Ok((syn::Type::Path(original), None)),
                        }
                    }
                    syn::Type::Path(arg_type_path) => {
                        let last = arg_type_path.path.segments.last().ok_or(syn::Error::new(
                            arg_type_path.span(),
                            "No last segment in type path",
                        ))?;
                        if last.ident == "Option" {
                            let (inner_ty, expr) = resolve_option_inner(arg_type_path)?;
                            let wrapped_ty = syn::parse_quote! {
                                ::pgrx::datum::VariadicArray<'_, #inner_ty>
                            };
                            Ok((wrapped_ty, expr))
                        } else {
                            Ok((syn::Type::Path(original), None))
                        }
                    }
                    _ => Ok((syn::Type::Path(original), None)),
                },
                _ => Ok((syn::Type::Path(original), None)),
            }
        }
        _ => Ok((syn::Type::Path(original), None)),
    }
}

fn resolve_array_inner(
    mut original: syn::TypePath,
) -> syn::Result<(syn::Type, Option<CompositeTypeMacro>)> {
    let original_span = original.span();
    let last = original
        .path
        .segments
        .last_mut()
        .ok_or(syn::Error::new(original_span, "Could not read last segment of path"))?;

    match last.arguments {
        syn::PathArguments::AngleBracketed(ref mut path_arg) => match path_arg.args.last() {
            Some(syn::GenericArgument::Type(ty)) => match ty.clone() {
                syn::Type::Macro(macro_pat) => {
                    let mac = &macro_pat.mac;
                    let archetype = mac.path.segments.last().expect("No last segment");
                    match archetype.ident.to_string().as_str() {
                            "default" => {
                                Err(syn::Error::new(mac.span(), "`VariadicArray<default!(T, default)>` not supported, choose `default!(VariadicArray<T>, ident)` instead"))
                            }
                            "composite_type" => {
                                let composite_mac = handle_composite_type_macro(mac)?;
                                let comp_ty = composite_mac.expand_with_lifetime();
                                let sql = Some(composite_mac);
                                let ty = syn::parse_quote! {
                                    ::pgrx::datum::Array<'_, #comp_ty>
                                };
                                Ok((ty, sql))
                            }
                            _ => Ok((syn::Type::Path(original), None)),
                        }
                }
                syn::Type::Path(arg_type_path) => {
                    let last = arg_type_path.path.segments.last().ok_or(syn::Error::new(
                        arg_type_path.span(),
                        "No last segment in type path",
                    ))?;
                    match last.ident.to_string().as_str() {
                        "Option" => {
                            let (inner_ty, expr) = resolve_option_inner(arg_type_path)?;
                            let wrapped_ty = syn::parse_quote! {
                                ::pgrx::datum::Array<'_, #inner_ty>
                            };
                            Ok((wrapped_ty, expr))
                        }
                        _ => Ok((syn::Type::Path(original), None)),
                    }
                }
                _ => Ok((syn::Type::Path(original), None)),
            },
            _ => Ok((syn::Type::Path(original), None)),
        },
        _ => Ok((syn::Type::Path(original), None)),
    }
}

fn resolve_option_inner(
    original: syn::TypePath,
) -> syn::Result<(syn::Type, Option<CompositeTypeMacro>)> {
    let segments = &original.path;
    let last = segments
        .segments
        .last()
        .ok_or(syn::Error::new(original.span(), "Could not read last segment of path"))?;

    match &last.arguments {
        syn::PathArguments::AngleBracketed(path_arg) => match path_arg.args.first() {
            Some(syn::GenericArgument::Type(ty)) => {
                match ty.clone() {
                    syn::Type::Macro(macro_pat) => {
                        let mac = &macro_pat.mac;
                        let archetype = mac.path.segments.last().expect("No last segment");
                        match archetype.ident.to_string().as_str() {
                            // Option<composite_type!(..)>
                            "composite_type" => {
                                let composite_mac = handle_composite_type_macro(mac)?;
                                let comp_ty = composite_mac.expand_with_lifetime();
                                let sql = Some(composite_mac);
                                let ty = syn::parse_quote! {
                                    Option<#comp_ty>
                                };
                                Ok((ty, sql))
                            },
                            // Option<default!(composite_type!(..))> isn't valid. If the user wanted the default to be `NULL` they just don't need a default.
                            "default" => Err(syn::Error::new(mac.span(), "`Option<default!(T, \"my_default\")>` not supported, choose `Option<T>` for a default of `NULL`, or `default!(T, default)` for a non-NULL default")),
                            _ => Ok((syn::Type::Path(original), None)),
                        }
                    }
                    syn::Type::Path(arg_type_path) => {
                        let last = arg_type_path.path.segments.last().ok_or(syn::Error::new(
                            arg_type_path.span(),
                            "No last segment in type path",
                        ))?;
                        match last.ident.to_string().as_str() {
                            // Option<Vec<composite_type!(..)>>
                            // Option<Vec<Option<composite_type!(..)>>>
                            "Vec" => {
                                let (inner_ty, expr) = resolve_vec_inner(arg_type_path)?;
                                let wrapped_ty = syn::parse_quote! {
                                    ::std::option::Option<#inner_ty>
                                };
                                Ok((wrapped_ty, expr))
                            }
                            // Option<VariadicArray<composite_type!(..)>>
                            // Option<VariadicArray<Option<composite_type!(..)>>>
                            "VariadicArray" => {
                                let (inner_ty, expr) = resolve_variadic_array_inner(arg_type_path)?;
                                let wrapped_ty = syn::parse_quote! {
                                    ::std::option::Option<#inner_ty>
                                };
                                Ok((wrapped_ty, expr))
                            }
                            // Option<Array<composite_type!(..)>>
                            // Option<Array<Option<composite_type!(..)>>>
                            "Array" => {
                                let (inner_ty, expr) = resolve_array_inner(arg_type_path)?;
                                let wrapped_ty = syn::parse_quote! {
                                    ::std::option::Option<#inner_ty>
                                };
                                Ok((wrapped_ty, expr))
                            }
                            // Option<..>
                            _ => Ok((syn::Type::Path(original), None)),
                        }
                    }
                    _ => Ok((syn::Type::Path(original), None)),
                }
            }
            _ => Ok((syn::Type::Path(original), None)),
        },
        _ => Ok((syn::Type::Path(original), None)),
    }
}

fn resolve_result_inner(
    original: syn::TypePath,
) -> syn::Result<(syn::Type, Option<CompositeTypeMacro>)> {
    let segments = &original.path;
    let last = segments
        .segments
        .last()
        .ok_or(syn::Error::new(original.span(), "Could not read last segment of path"))?;

    // Get the path of our Result type, to handle crate::module::Result pattern
    let mut without_type_args = original.path.clone();
    without_type_args.segments.last_mut().unwrap().arguments = syn::PathArguments::None;

    let (ok_ty, err_ty) = {
        if let syn::PathArguments::AngleBracketed(path_arg) = last.arguments.clone() {
            let mut iter = path_arg.args.into_iter();
            match (iter.next(), iter.next()) {
                (None, _) => {
                    // Return early, Result<> with no type args.
                    return Err(syn::Error::new(
                        last.arguments.span(),
                        "Cannot return a Result without type generic arguments.",
                    ));
                }
                // Since `pub type Result<T> = std::error::Result<T, OurError>
                // is a common pattern,
                // we should support single-argument Result<T> style.
                (Some(first_ty), None) => (first_ty, None),
                // This is the more common Result<T, E>.
                (Some(first_ty), Some(second_ty)) => (first_ty, Some(second_ty)),
            }
        } else {
            // Return early, invalid signature for Result<T,E>
            return Err(syn::Error::new(
                last.arguments.span(),
                "Cannot return a Result without type generic arguments.",
            ));
        }
    };

    // Inner / nested function for getting a type signature for a Result from
    // the tuple of (ok_type, Option<error_type>)
    fn type_for_args(
        no_args_path: syn::Path,
        first_ty: syn::Type,
        err_ty: Option<GenericArgument>,
    ) -> syn::Type {
        match err_ty {
            Some(e) => {
                syn::parse_quote! {
                    #no_args_path<#first_ty, #e>
                }
            }
            None => {
                // Since `pub type Result<T> = std::error::Result<T, OurError>
                // is a common pattern,
                // we should support single-argument Result<T> style.
                syn::parse_quote! {
                    #no_args_path<#first_ty>
                }
            }
        }
    }

    match &ok_ty {
        syn::GenericArgument::Type(ty) => {
            match ty.clone() {
                syn::Type::Macro(macro_pat) => {
                    let mac = &macro_pat.mac;
                    let archetype = mac.path.segments.last().expect("No last segment");
                    match archetype.ident.to_string().as_str() {
                        // Result<composite_type!(..), E>
                        "composite_type" => {
                            let composite_mac = handle_composite_type_macro(mac)?;
                            let comp_ty = composite_mac.expand_with_lifetime();
                            let sql = Some(composite_mac);

                            let ty = type_for_args(without_type_args, comp_ty, err_ty);
                            Ok((ty, sql))
                        },
                        // Result<default!(composite_type!(..)), E> 
                        "default" => {
                            Err(syn::Error::new(mac.span(), "`Result<default!(T, default), E>` not supported, choose `default!(Result<T, E>, ident)` instead"))
                        },
                        _ => Ok((syn::Type::Path(original), None)),
                    }
                }
                syn::Type::Path(arg_type_path) => {
                    let last = arg_type_path.path.segments.last().ok_or(syn::Error::new(
                        arg_type_path.span(),
                        "No last segment in type path",
                    ))?;
                    match last.ident.to_string().as_str() {
                        // Result<Option<composite_type!(..)>>
                        // Result<Option<Vec<composite_type!(..)>>>>
                        "Option" => {
                            let (inner_ty, expr) = resolve_option_inner(arg_type_path)?;
                            let wrapped_ty = type_for_args(without_type_args, inner_ty, err_ty);
                            Ok((wrapped_ty, expr))
                        }
                        // Result<Vec<composite_type!(..)>>
                        // Result<Vec<Option<composite_type!(..)>>>
                        "Vec" => {
                            let (inner_ty, expr) = resolve_vec_inner(arg_type_path)?;
                            let wrapped_ty = type_for_args(without_type_args, inner_ty, err_ty);
                            Ok((wrapped_ty, expr))
                        }
                        // Result<VariadicArray<composite_type!(..)>>
                        // Result<VariadicArray<Option<composite_type!(..)>>>
                        "VariadicArray" => {
                            let (inner_ty, expr) = resolve_variadic_array_inner(arg_type_path)?;
                            let wrapped_ty = type_for_args(without_type_args, inner_ty, err_ty);
                            Ok((wrapped_ty, expr))
                        }
                        // Result<Array<composite_type!(..)>>
                        // Result<Array<Option<composite_type!(..)>>>
                        "Array" => {
                            let (inner_ty, expr) = resolve_array_inner(arg_type_path)?;
                            let wrapped_ty = type_for_args(without_type_args, inner_ty, err_ty);
                            Ok((wrapped_ty, expr))
                        }
                        // Result<T> where T is plain-old-data and not a (supported) container type.
                        _ => Ok((syn::Type::Path(original), None)),
                    }
                }
                _ => Ok((syn::Type::Path(original), None)),
            }
        }
        _ => Ok((syn::Type::Path(original), None)),
    }
}

fn handle_default_macro(mac: &syn::Macro) -> syn::Result<(syn::Type, Option<String>)> {
    let out: DefaultMacro = mac.parse_body()?;
    let true_ty = out.ty;
    match out.expr {
        syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Str(def), .. }) => {
            let value = def.value();
            Ok((true_ty, Some(value)))
        }
        syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Float(def), .. }) => {
            let value = def.base10_digits();
            Ok((true_ty, Some(value.to_string())))
        }
        syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Int(def), .. }) => {
            let value = def.base10_digits();
            Ok((true_ty, Some(value.to_string())))
        }
        syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Bool(def), .. }) => {
            let value = def.value();
            Ok((true_ty, Some(value.to_string())))
        }
        syn::Expr::Unary(syn::ExprUnary { op: syn::UnOp::Neg(_), ref expr, .. }) => match &**expr {
            syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Int(def), .. }) => {
                let value = def.base10_digits();
                Ok((true_ty, Some("-".to_owned() + value)))
            }
            // FIXME: add a UI test for this
            _ => Err(syn::Error::new(
                mac.span(),
                format!("Unrecognized UnaryExpr in `default!()` macro, got: {:?}", out.expr),
            )),
        },
        syn::Expr::Path(syn::ExprPath { path: syn::Path { ref segments, .. }, .. }) => {
            let last = segments.last().expect("No last segment");
            let last_string = last.ident.to_string();
            if last_string == "NULL" {
                Ok((true_ty, Some(last_string)))
            } else {
                // FIXME: add a UI test for this
                Err(syn::Error::new(
                    mac.span(),
                    format!(
                        "Unable to parse default value of `default!()` macro, got: {:?}",
                        out.expr
                    ),
                ))
            }
        }
        // FIXME: add a UI test for this
        _ => Err(syn::Error::new(
            mac.span(),
            format!("Unable to parse default value of `default!()` macro, got: {:?}", out.expr),
        )),
    }
}

#[derive(Debug, Clone)]
pub(crate) struct DefaultMacro {
    ty: syn::Type,
    pub(crate) expr: syn::Expr,
}

impl Parse for DefaultMacro {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ty = input.parse()?;
        let _comma: Token![,] = input.parse()?;
        let expr = input.parse()?;
        Ok(Self { ty, expr })
    }
}
