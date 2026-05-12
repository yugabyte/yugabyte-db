/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * documentdb_macros/src/lib.rs
 *
 *-------------------------------------------------------------------------
 */

extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn documentdb_int_error_mapping(_item: TokenStream) -> TokenStream {
    let mut result = String::new();
    let path = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../pg_documentdb_core/include/utils/external_error_mapping.csv");
    let csv = std::fs::File::open(path).unwrap();
    let reader = std::io::BufReader::new(csv);

    result += "pub fn from_known_external_error_code(state: &SqlState) -> Option<(i32, &str)> {
                match state.code() {";
    for line in std::io::BufRead::lines(reader).skip(1) {
        let line = line.unwrap();
        let parts: Vec<&str> = line.split(',').collect();
        result += &format!(
            "\"{}\" => Some(({}, \"{}\")),",
            parts[1], parts[2], parts[0]
        );
    }
    result += "_ => None
    }
    }";
    result.parse().unwrap()
}

// In the gateway we deal with define known errors in two files, one located in the backend and one in the gateway
// to add a logical separation between the two. This macro will generate an enum with all the error codes
// defined in the two files, so that we can use it in the code.
// The macro will also generate a from_i32 and from_u32 methods to convert from the error code to the enum variant.
#[proc_macro]
pub fn documentdb_error_code_enum(_item: TokenStream) -> TokenStream {
    let external_error_mapping_path = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../pg_documentdb_core/include/utils/external_error_mapping.csv");

    let csv = std::fs::read_to_string(&external_error_mapping_path)
        .expect("Could not read external_error_mapping.csv");
    let mut error_code_enum_entries = String::new();
    error_code_enum_entries += "#[derive(Debug, Clone, Copy)]
        pub enum ErrorCode {";

    let mut from_primitive = String::new();
    from_primitive += "impl ErrorCode {
             pub fn from_i32(n: i32) -> Option<Self> {
                 match n {";

    for external_error in csv.lines().skip(1) {
        let parts: Vec<&str> = external_error.split(',').collect();
        let name = parts[0].trim();
        let code = parts[2].trim();
        error_code_enum_entries += &format!("{name} = {code},");
        from_primitive += &format!("{code} => Some(ErrorCode::{name}),");
    }

    let gateway_error_mapping = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../pg_documentdb_gw/include/gateway_errors.txt");
    let csv =
        std::fs::read_to_string(&gateway_error_mapping).expect("Could not read gateway_errors.txt");

    for gateway_error in csv.lines().skip(1) {
        let parts: Vec<&str> = gateway_error.split(',').collect();
        let name = parts[0].trim();
        let code = parts[2].trim();
        error_code_enum_entries += &format!("{name} = {code},");
        from_primitive += &format!("{code} => Some(ErrorCode::{name}),");
    }

    error_code_enum_entries += "
    }
    ";

    from_primitive += "_ => None,
        }
    }

    pub fn from_u32(n: u32) -> Option<Self> {
        Self::from_i32(n as i32)
    }
}";

    error_code_enum_entries += &from_primitive;
    error_code_enum_entries.parse().unwrap()
}
