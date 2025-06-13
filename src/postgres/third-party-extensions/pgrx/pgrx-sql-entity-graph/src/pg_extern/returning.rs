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

`#[pg_extern]` return value related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use super::LastIdent;
use crate::UsedType;

use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens, TokenStreamExt};
use syn::parse::{Parse, ParseStream};

use syn::spanned::Spanned;
use syn::{Error, GenericArgument, PathArguments, Token, Type};

#[derive(Debug, Clone)]
pub struct ReturningIteratedItem {
    pub used_ty: UsedType,
    pub name: Option<String>,
}

#[derive(Debug, Clone)]
pub enum Returning {
    None,
    Type(UsedType),
    SetOf { ty: UsedType },
    Iterated { tys: Vec<ReturningIteratedItem> },
    // /// Technically we don't ever create this, single triggers have their own macro.
    // Trigger,
}

impl Returning {
    fn parse_type_macro(type_macro: &mut syn::TypeMacro) -> Result<Returning, syn::Error> {
        let mac = &type_macro.mac;
        let opt_archetype = mac.path.segments.last().map(|archetype| archetype.ident.to_string());
        match opt_archetype.as_deref() {
            Some("composite_type") => {
                Ok(Returning::Type(UsedType::new(syn::Type::Macro(type_macro.clone()))?))
            }
            _ => Err(syn::Error::new(
                type_macro.span(),
                "type macros other than `composite_type!` are not yet implemented",
            )),
        }
    }

    fn match_type(ty: &Type) -> Result<Returning, Error> {
        let mut ty = Box::new(ty.clone());

        match &mut *ty {
            syn::Type::Path(typepath) => {
                let is_option = typepath.last_ident_is("Option");
                let is_result = typepath.last_ident_is("Result");
                let mut is_setof_iter = typepath.last_ident_is("SetOfIterator");
                let mut is_table_iter = typepath.last_ident_is("TableIterator");
                let path = &mut typepath.path;

                if is_option || is_result || is_setof_iter || is_table_iter {
                    let option_inner_path = if is_option || is_result {
                        match path.segments.last_mut().map(|s| &mut s.arguments) {
                            Some(syn::PathArguments::AngleBracketed(args)) => {
                                let args_span = args.span();
                                match args.args.first_mut() {
                                    Some(syn::GenericArgument::Type(syn::Type::Path(syn::TypePath { qself: _, path }))) => path.clone(),
                                    Some(syn::GenericArgument::Type(_)) => {
                                        let used_ty = UsedType::new(syn::Type::Path(typepath.clone()))?;
                                        return Ok(Returning::Type(used_ty))
                                    },
                                    other => {
                                        return Err(syn::Error::new(
                                            other.as_ref().map(|s| s.span()).unwrap_or(args_span),
                                            format!(
                                                "Got unexpected generic argument for Option inner: {other:?}"
                                            ),
                                        ))
                                    }
                                }
                            }
                            other => {
                                return Err(syn::Error::new(
                                    other.span(),
                                    format!(
                                        "Got unexpected path argument for Option inner: {other:?}"
                                    ),
                                ))
                            }
                        }
                    } else {
                        path.clone()
                    };

                    let mut segments = option_inner_path.segments.clone();

                    loop {
                        if let Some(segment) = segments.filter_last_ident("Option") {
                            let PathArguments::AngleBracketed(generics) = &segment.arguments else {
                                unreachable!()
                            };
                            let Some(GenericArgument::Type(Type::Path(this_path))) =
                                generics.args.last()
                            else {
                                return Err(syn::Error::new_spanned(
                                    generics,
                                    "where's the generic args?",
                                ));
                            };
                            segments = this_path.path.segments.clone(); // recurse deeper
                        } else {
                            if segments.last_ident_is("SetOfIterator") {
                                is_setof_iter = true;
                            } else if segments.last_ident_is("TableIterator") {
                                is_table_iter = true;
                            }
                            break;
                        }
                    }

                    if is_setof_iter {
                        let last_path_segment = option_inner_path.segments.last();
                        let used_ty = match &last_path_segment.map(|ps| &ps.arguments) {
                            Some(syn::PathArguments::AngleBracketed(args)) => {
                                match args.args.last().expect("should have one arg?") {
                                    syn::GenericArgument::Type(ty) => {
                                        match ty {
                                            Type::Path(_) | Type::Macro(_) | Type::Reference(_) => UsedType::new(ty.clone())?,
                                            ty => return Err(syn::Error::new(
                                                ty.span(),
                                                "SetOf Iterator must have an item",
                                            )),
                                        }
                                    }
                                    other => {
                                        return Err(syn::Error::new(
                                            other.span(),
                                            format!(
                                                "Got unexpected generic argument for SetOfIterator: {other:?}"
                                            ),
                                        ))
                                    }
                                }
                            }
                            other => {
                                return Err(syn::Error::new(
                                    other
                                        .map(|s| s.span())
                                        .unwrap_or_else(proc_macro2::Span::call_site),
                                    format!(
                                        "Got unexpected path argument for SetOfIterator: {other:?}"
                                    ),
                                ))
                            }
                        };
                        Ok(Returning::SetOf { ty: used_ty })
                    } else if is_table_iter {
                        let last_path_segment = segments.last_mut().unwrap();
                        let mut iterated_items = vec![];

                        match &mut last_path_segment.arguments {
                            syn::PathArguments::AngleBracketed(args) => {
                                match args.args.last_mut().unwrap() {
                                    syn::GenericArgument::Type(syn::Type::Tuple(type_tuple)) => {
                                        for elem in &type_tuple.elems {
                                            match &elem {
                                                syn::Type::Path(path) => {
                                                    let iterated_item = ReturningIteratedItem {
                                                        name: None,
                                                        used_ty: UsedType::new(syn::Type::Path(
                                                            path.clone(),
                                                        ))?,
                                                    };
                                                    iterated_items.push(iterated_item);
                                                }
                                                syn::Type::Macro(type_macro) => {
                                                    let mac = &type_macro.mac;
                                                    let archetype =
                                                        mac.path.segments.last().unwrap();
                                                    match archetype.ident.to_string().as_str() {
                                                        "name" => {
                                                            let out: NameMacro =
                                                                mac.parse_body()?;
                                                            let iterated_item =
                                                                ReturningIteratedItem {
                                                                    name: Some(out.ident),
                                                                    used_ty: out.used_ty,
                                                                };
                                                            iterated_items.push(iterated_item)
                                                        }
                                                        _ => {
                                                            let iterated_item =
                                                                ReturningIteratedItem {
                                                                    name: None,
                                                                    used_ty: UsedType::new(
                                                                        syn::Type::Macro(
                                                                            type_macro.clone(),
                                                                        ),
                                                                    )?,
                                                                };
                                                            iterated_items.push(iterated_item);
                                                        }
                                                    }
                                                }
                                                reference @ syn::Type::Reference(_) => {
                                                    let iterated_item = ReturningIteratedItem {
                                                        name: None,
                                                        used_ty: UsedType::new(
                                                            (*reference).clone(),
                                                        )?,
                                                    };
                                                    iterated_items.push(iterated_item);
                                                }
                                                ty => {
                                                    return Err(syn::Error::new(
                                                        ty.span(),
                                                        "Table Iterator must have an item",
                                                    ));
                                                }
                                            };
                                        }
                                    }
                                    syn::GenericArgument::Lifetime(_) => (),
                                    other => {
                                        return Err(syn::Error::new(
                                            other.span(),
                                            format!("Got unexpected generic argument: {other:?}"),
                                        ))
                                    }
                                };
                            }
                            other => {
                                return Err(syn::Error::new(
                                    other.span(),
                                    format!("Got unexpected path argument: {other:?}"),
                                ))
                            }
                        };
                        Ok(Returning::Iterated { tys: iterated_items })
                    } else {
                        let used_ty = UsedType::new(syn::Type::Path(typepath.clone()))?;
                        Ok(Returning::Type(used_ty))
                    }
                } else {
                    let used_ty = UsedType::new(syn::Type::Path(typepath.clone()))?;
                    Ok(Returning::Type(used_ty))
                }
            }
            syn::Type::Reference(ty_ref) => {
                let used_ty = UsedType::new(syn::Type::Reference(ty_ref.clone()))?;
                Ok(Returning::Type(used_ty))
            }
            syn::Type::Macro(ref mut type_macro) => Self::parse_type_macro(type_macro),
            syn::Type::Paren(ref mut type_paren) => match &mut *type_paren.elem {
                syn::Type::Macro(ref mut type_macro) => Self::parse_type_macro(type_macro),
                other => Err(syn::Error::new(
                    other.span(),
                    format!("Got unknown return type (type_paren): {type_paren:?}"),
                )),
            },
            syn::Type::Group(tg) => Self::match_type(&tg.elem),
            other => Err(syn::Error::new(
                other.span(),
                format!("Got unknown return type (other): {other:?}"),
            )),
        }
    }
}

impl TryFrom<&syn::ReturnType> for Returning {
    type Error = syn::Error;

    fn try_from(value: &syn::ReturnType) -> Result<Self, Self::Error> {
        match &value {
            syn::ReturnType::Default => Ok(Returning::None),
            syn::ReturnType::Type(_, ty) => Self::match_type(ty),
        }
    }
}

impl ToTokens for Returning {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let quoted = match self {
            Returning::None => quote! {
                ::pgrx::pgrx_sql_entity_graph::PgExternReturnEntity::None
            },
            Returning::Type(used_ty) => {
                let used_ty_entity_tokens = used_ty.entity_tokens();
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::PgExternReturnEntity::Type {
                        ty: #used_ty_entity_tokens,
                    }
                }
            }
            Returning::SetOf { ty: used_ty } => {
                let used_ty_entity_tokens = used_ty.entity_tokens();
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::PgExternReturnEntity::SetOf {
                        ty: #used_ty_entity_tokens,
                                                                  }
                }
            }
            Returning::Iterated { tys: items } => {
                let quoted_items = items
                    .iter()
                    .map(|ReturningIteratedItem { used_ty, name }| {
                        let name_iter = name.iter();
                        let used_ty_entity_tokens = used_ty.entity_tokens();
                        quote! {
                            ::pgrx::pgrx_sql_entity_graph::PgExternReturnEntityIteratedItem {
                                ty: #used_ty_entity_tokens,
                                name: None #( .unwrap_or(Some(stringify!(#name_iter))) )*,
                            }
                        }
                    })
                    .collect::<Vec<_>>();
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::PgExternReturnEntity::Iterated {
                        tys: vec![
                            #(#quoted_items),*
                        ],
                    }
                }
            }
        };
        tokens.append_all(quoted);
    }
}

#[derive(Debug, Clone)]
pub struct NameMacro {
    pub ident: String,
    pub used_ty: UsedType,
}

impl Parse for NameMacro {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ident = input
            .parse::<syn::Ident>()
            .map(|v| v.to_string())
            // Avoid making folks unable to use rust keywords.
            .or_else(|_| input.parse::<syn::Token![type]>().map(|_| String::from("type")))
            .or_else(|_| input.parse::<syn::Token![mod]>().map(|_| String::from("mod")))
            .or_else(|_| input.parse::<syn::Token![extern]>().map(|_| String::from("extern")))
            .or_else(|_| input.parse::<syn::Token![async]>().map(|_| String::from("async")))
            .or_else(|_| input.parse::<syn::Token![crate]>().map(|_| String::from("crate")))
            .or_else(|_| input.parse::<syn::Token![use]>().map(|_| String::from("use")))?;
        let _comma: Token![,] = input.parse()?;
        let ty: syn::Type = input.parse()?;

        let used_ty = UsedType::new(ty)?;

        Ok(Self { ident, used_ty })
    }
}
