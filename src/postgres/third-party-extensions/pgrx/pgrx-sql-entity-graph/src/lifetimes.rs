//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

pub fn anonymize_lifetimes_in_type_path(value: syn::TypePath) -> syn::TypePath {
    let mut ty = syn::Type::Path(value);
    anonymize_lifetimes(&mut ty);
    match ty {
        syn::Type::Path(type_path) => type_path,

        // shouldn't happen
        _ => panic!("not a TypePath"),
    }
}

pub fn anonymize_lifetimes(value: &mut syn::Type) {
    match value {
        syn::Type::Path(syn::TypePath { path: syn::Path { segments, .. }, .. }) => segments
            .iter_mut()
            .filter_map(|segment| match &mut segment.arguments {
                syn::PathArguments::AngleBracketed(bracketed) => Some(bracketed),
                _ => None,
            })
            .flat_map(|bracketed| &mut bracketed.args)
            .for_each(|arg| match arg {
                // rename lifetimes to the anonymous lifetime
                syn::GenericArgument::Lifetime(lifetime) => {
                    lifetime.ident = syn::Ident::new("_", lifetime.ident.span());
                }
                // recurse
                syn::GenericArgument::Type(ty) => anonymize_lifetimes(ty),
                syn::GenericArgument::AssocType(binding) => anonymize_lifetimes(&mut binding.ty),
                syn::GenericArgument::Constraint(constraint) => {
                    constraint.bounds.iter_mut().for_each(|bound| {
                        if let syn::TypeParamBound::Lifetime(lifetime) = bound {
                            lifetime.ident = syn::Ident::new("_", lifetime.ident.span())
                        }
                    })
                }
                // nothing to do otherwise
                _ => {}
            }),

        syn::Type::Reference(type_ref) => {
            if let Some(lifetime) = type_ref.lifetime.as_mut() {
                lifetime.ident = syn::Ident::new("_", lifetime.ident.span());
            }
        }
        syn::Type::Tuple(type_tuple) => type_tuple.elems.iter_mut().for_each(anonymize_lifetimes),
        _ => {}
    }
}
