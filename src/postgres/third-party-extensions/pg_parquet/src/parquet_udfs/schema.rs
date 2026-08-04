use crate::arrow_parquet::uri_utils::{
    ensure_access_privilege_to_uri, parquet_schema_from_uri, uri_as_string, ParsedUriInfo,
};

use ::parquet::{
    format::{ConvertedType, FieldRepetitionType, LogicalType, Type},
    schema::types::to_thrift,
};
use pgrx::{iter::TableIterator, name, pg_extern, pg_schema};

#[pg_schema]
mod parquet {
    use super::*;

    #[pg_extern]
    #[allow(clippy::type_complexity)]
    fn schema(
        uri: String,
    ) -> TableIterator<
        'static,
        (
            name!(uri, String),
            name!(name, String),
            name!(type_name, Option<String>),
            name!(type_length, Option<String>),
            name!(repetition_type, Option<String>),
            name!(num_children, Option<i32>),
            name!(converted_type, Option<String>),
            name!(scale, Option<i32>),
            name!(precision, Option<i32>),
            name!(field_id, Option<i32>),
            name!(logical_type, Option<String>),
        ),
    > {
        let uri_info = ParsedUriInfo::try_from(uri.as_str()).unwrap_or_else(|e| {
            panic!("{}", e.to_string());
        });

        ensure_access_privilege_to_uri(&uri_info.uri, true);
        let parquet_schema = parquet_schema_from_uri(&uri_info);

        let root_type = parquet_schema.root_schema();
        let thrift_schema_elements = to_thrift(root_type).unwrap_or_else(|e| {
            panic!("Failed to convert schema to thrift: {}", e);
        });

        let mut rows = vec![];

        for schema_elem in thrift_schema_elements {
            let name = schema_elem.name;

            let type_name = schema_elem.type_.map(thrift_type_to_str);

            let type_length = schema_elem.type_length.map(|t| t.to_string());

            let repetition_type = schema_elem
                .repetition_type
                .map(thrift_repetition_type_to_str);

            let num_children = schema_elem.num_children;

            let converted_type = schema_elem.converted_type.map(thrift_converted_type_to_str);

            let scale = schema_elem.scale;

            let precision = schema_elem.precision;

            let field_id = schema_elem.field_id;

            let logical_type = schema_elem.logical_type.map(thrift_logical_type_to_str);

            let row = (
                uri_as_string(&uri_info.uri),
                name,
                type_name,
                type_length,
                repetition_type,
                num_children,
                converted_type,
                scale,
                precision,
                field_id,
                logical_type,
            );

            rows.push(row);
        }

        TableIterator::new(rows)
    }
}

fn thrift_type_to_str(thrift_type: Type) -> String {
    match thrift_type {
        Type::BOOLEAN => "BOOLEAN",
        Type::INT32 => "INT32",
        Type::INT64 => "INT64",
        Type::INT96 => "INT96",
        Type::FLOAT => "FLOAT",
        Type::DOUBLE => "DOUBLE",
        Type::BYTE_ARRAY => "BYTE_ARRAY",
        Type::FIXED_LEN_BYTE_ARRAY => "FIXED_LEN_BYTE_ARRAY",
        _ => "UNKNOWN",
    }
    .into()
}

fn thrift_repetition_type_to_str(repetition_type: FieldRepetitionType) -> String {
    match repetition_type {
        FieldRepetitionType::REQUIRED => "REQUIRED",
        FieldRepetitionType::OPTIONAL => "OPTIONAL",
        FieldRepetitionType::REPEATED => "REPEATED",
        _ => "UNKNOWN",
    }
    .into()
}

fn thrift_logical_type_to_str(logical_type: LogicalType) -> String {
    match logical_type {
        LogicalType::STRING(_) => "STRING",
        LogicalType::MAP(_) => "MAP",
        LogicalType::LIST(_) => "LIST",
        LogicalType::ENUM(_) => "ENUM",
        LogicalType::DECIMAL(_) => "DECIMAL",
        LogicalType::DATE(_) => "DATE",
        LogicalType::TIME(_) => "TIME",
        LogicalType::TIMESTAMP(_) => "TIMESTAMP",
        LogicalType::INTEGER(_) => "INTEGER",
        LogicalType::UNKNOWN(_) => "UNKNOWN",
        LogicalType::JSON(_) => "JSON",
        LogicalType::BSON(_) => "BSON",
        LogicalType::UUID(_) => "UUID",
        LogicalType::FLOAT16(_) => "FLOAT16",
    }
    .into()
}

fn thrift_converted_type_to_str(converted_type: ConvertedType) -> String {
    match converted_type {
        ConvertedType::UTF8 => "UTF8",
        ConvertedType::MAP => "MAP",
        ConvertedType::MAP_KEY_VALUE => "MAP_KEY_VALUE",
        ConvertedType::LIST => "LIST",
        ConvertedType::ENUM => "ENUM",
        ConvertedType::DECIMAL => "DECIMAL",
        ConvertedType::DATE => "DATE",
        ConvertedType::TIME_MILLIS => "TIME_MILLIS",
        ConvertedType::TIME_MICROS => "TIME_MICROS",
        ConvertedType::TIMESTAMP_MILLIS => "TIMESTAMP_MILLIS",
        ConvertedType::TIMESTAMP_MICROS => "TIMESTAMP_MICROS",
        ConvertedType::UINT_8 => "UINT_8",
        ConvertedType::UINT_16 => "UINT_16",
        ConvertedType::UINT_32 => "UINT_32",
        ConvertedType::UINT_64 => "UINT_64",
        ConvertedType::INT_8 => "INT_8",
        ConvertedType::INT_16 => "INT_16",
        ConvertedType::INT_32 => "INT_32",
        ConvertedType::INT_64 => "INT_64",
        ConvertedType::JSON => "JSON",
        ConvertedType::BSON => "BSON",
        ConvertedType::INTERVAL => "INTERVAL",
        _ => "UNKOWN",
    }
    .into()
}
