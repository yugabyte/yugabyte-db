//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens, TokenStreamExt};

pub struct CodeEnrichment<T>(pub T);

/// Generates the rust code that pgrx requires for one of its SQL interfaces such as `#[pg_extern]`
pub trait ToRustCodeTokens {
    fn to_rust_code_tokens(&self) -> TokenStream2 {
        quote! {}
    }
}

/// Generates the rust code to tie one of pgrx' supported SQL interfaces into pgrx' schema generator
pub trait ToEntityGraphTokens {
    fn to_entity_graph_tokens(&self) -> TokenStream2;
}

impl<T> ToTokens for CodeEnrichment<T>
where
    T: ToEntityGraphTokens + ToRustCodeTokens,
{
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        #[cfg(not(feature = "no-schema-generation"))]
        {
            // only emit entity graph tokens when we're generating a schema, which is our default mode
            tokens.append_all(self.0.to_entity_graph_tokens());
        }

        tokens.append_all(self.0.to_rust_code_tokens());
    }
}
