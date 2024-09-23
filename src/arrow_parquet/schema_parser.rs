use std::{collections::HashMap, ops::Deref, sync::Arc};

use arrow::datatypes::{Field, Fields, Schema};
use arrow_schema::FieldRef;
use parquet::arrow::{arrow_to_parquet_schema, PARQUET_FIELD_ID_META_KEY};
use pg_sys::{
    Oid, BOOLOID, BYTEAOID, CHAROID, DATEOID, FLOAT4OID, FLOAT8OID, INT2OID, INT4OID, INT8OID,
    NUMERICOID, OIDOID, TEXTOID, TIMEOID, TIMESTAMPOID, TIMESTAMPTZOID, TIMETZOID,
};
use pgrx::{check_for_interrupts, prelude::*, PgTupleDesc};

use crate::{
    pgrx_utils::{
        array_element_typoid, collect_valid_attributes, domain_array_base_elem_typoid,
        is_array_type, is_composite_type, tuple_desc,
    },
    type_compat::{
        geometry::is_postgis_geometry_type,
        map::is_map_type,
        pg_arrow_type_conversions::{
            extract_precision_from_numeric_typmod, extract_scale_from_numeric_typmod,
            MAX_DECIMAL_PRECISION,
        },
    },
};

pub(crate) fn parquet_schema_string_from_tupledesc(tupledesc: &PgTupleDesc) -> String {
    let arrow_schema = parse_arrow_schema_from_tupledesc(tupledesc);
    let parquet_schema = arrow_to_parquet_schema(&arrow_schema)
        .unwrap_or_else(|e| panic!("failed to convert arrow schema to parquet schema: {}", e));

    let mut buf = Vec::new();
    parquet::schema::printer::print_schema(&mut buf, &parquet_schema.root_schema_ptr());
    String::from_utf8(buf).unwrap_or_else(|e| panic!("failed to convert schema to string: {}", e))
}

pub(crate) fn parse_arrow_schema_from_tupledesc(tupledesc: &PgTupleDesc) -> Schema {
    let mut field_id = 0;

    let mut struct_attribute_fields = vec![];

    let include_generated_columns = true;
    let attributes = collect_valid_attributes(tupledesc, include_generated_columns);

    for attribute in attributes {
        let attribute_name = attribute.name();
        let attribute_typoid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let field = if is_composite_type(attribute_typoid) {
            let attribute_tupledesc = tuple_desc(attribute_typoid, attribute_typmod);
            parse_struct_schema(attribute_tupledesc, attribute_name, &mut field_id)
        } else if is_map_type(attribute_typoid) {
            let attribute_base_elem_typoid = domain_array_base_elem_typoid(attribute_typoid);
            parse_map_schema(
                attribute_base_elem_typoid,
                attribute_typmod,
                attribute_name,
                &mut field_id,
            )
        } else if is_array_type(attribute_typoid) {
            let attribute_element_typoid = array_element_typoid(attribute_typoid);
            parse_list_schema(
                attribute_element_typoid,
                attribute_typmod,
                attribute_name,
                &mut field_id,
            )
        } else {
            parse_primitive_schema(
                attribute_typoid,
                attribute_typmod,
                attribute_name,
                &mut field_id,
            )
        };

        struct_attribute_fields.push(field);
    }

    Schema::new(Fields::from(struct_attribute_fields))
}

fn parse_struct_schema(tupledesc: PgTupleDesc, elem_name: &str, field_id: &mut i32) -> Arc<Field> {
    check_for_interrupts!();

    let metadata = HashMap::from_iter(vec![(
        PARQUET_FIELD_ID_META_KEY.into(),
        field_id.to_string(),
    )]);

    *field_id += 1;

    let mut child_fields: Vec<Arc<Field>> = vec![];

    let include_generated_columns = true;
    let attributes = collect_valid_attributes(&tupledesc, include_generated_columns);

    for attribute in attributes {
        if attribute.is_dropped() {
            continue;
        }

        let attribute_name = attribute.name();
        let attribute_oid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let child_field = if is_composite_type(attribute_oid) {
            let attribute_tupledesc = tuple_desc(attribute_oid, attribute_typmod);
            parse_struct_schema(attribute_tupledesc, attribute_name, field_id)
        } else if is_map_type(attribute_oid) {
            let attribute_base_elem_typoid = domain_array_base_elem_typoid(attribute_oid);
            parse_map_schema(
                attribute_base_elem_typoid,
                attribute_typmod,
                attribute_name,
                field_id,
            )
        } else if is_array_type(attribute_oid) {
            let attribute_element_typoid = array_element_typoid(attribute_oid);
            parse_list_schema(
                attribute_element_typoid,
                attribute_typmod,
                attribute_name,
                field_id,
            )
        } else {
            parse_primitive_schema(attribute_oid, attribute_typmod, attribute_name, field_id)
        };

        child_fields.push(child_field);
    }

    Field::new(
        elem_name,
        arrow::datatypes::DataType::Struct(Fields::from(child_fields)),
        true,
    )
    .with_metadata(metadata)
    .into()
}

fn parse_list_schema(typoid: Oid, typmod: i32, array_name: &str, field_id: &mut i32) -> Arc<Field> {
    check_for_interrupts!();

    let list_metadata = HashMap::from_iter(vec![(
        PARQUET_FIELD_ID_META_KEY.into(),
        field_id.to_string(),
    )]);

    *field_id += 1;

    let elem_field = if is_composite_type(typoid) {
        let tupledesc = tuple_desc(typoid, typmod);
        parse_struct_schema(tupledesc, array_name, field_id)
    } else if is_map_type(typoid) {
        let base_elem_typoid = domain_array_base_elem_typoid(typoid);
        parse_map_schema(base_elem_typoid, typmod, array_name, field_id)
    } else {
        parse_primitive_schema(typoid, typmod, array_name, field_id)
    };

    Field::new(
        array_name,
        arrow::datatypes::DataType::List(elem_field),
        true,
    )
    .with_metadata(list_metadata)
    .into()
}

fn parse_map_schema(typoid: Oid, typmod: i32, map_name: &str, field_id: &mut i32) -> Arc<Field> {
    let map_metadata = HashMap::from_iter(vec![(
        PARQUET_FIELD_ID_META_KEY.into(),
        field_id.to_string(),
    )]);

    *field_id += 1;

    let tupledesc = tuple_desc(typoid, typmod);
    let entries_field = parse_struct_schema(tupledesc, map_name, field_id);
    let entries_field = adjust_map_entries_field(entries_field);

    Field::new(
        map_name,
        arrow::datatypes::DataType::Map(entries_field, false),
        true,
    )
    .with_metadata(map_metadata)
    .into()
}

fn parse_primitive_schema(
    typoid: Oid,
    typmod: i32,
    elem_name: &str,
    field_id: &mut i32,
) -> Arc<Field> {
    check_for_interrupts!();

    let primitive_metadata = HashMap::<String, String>::from_iter(vec![(
        PARQUET_FIELD_ID_META_KEY.into(),
        field_id.to_string(),
    )]);

    *field_id += 1;

    let field = match typoid {
        FLOAT4OID => Field::new(elem_name, arrow::datatypes::DataType::Float32, true),
        FLOAT8OID => Field::new(elem_name, arrow::datatypes::DataType::Float64, true),
        BOOLOID => Field::new(elem_name, arrow::datatypes::DataType::Boolean, true),
        INT2OID => Field::new(elem_name, arrow::datatypes::DataType::Int16, true),
        INT4OID => Field::new(elem_name, arrow::datatypes::DataType::Int32, true),
        INT8OID => Field::new(elem_name, arrow::datatypes::DataType::Int64, true),
        NUMERICOID => {
            let precision = extract_precision_from_numeric_typmod(typmod);
            let scale = extract_scale_from_numeric_typmod(typmod);

            if precision > MAX_DECIMAL_PRECISION {
                Field::new(elem_name, arrow::datatypes::DataType::Utf8, true)
            } else {
                Field::new(
                    elem_name,
                    arrow::datatypes::DataType::Decimal128(precision as _, scale as _),
                    true,
                )
            }
        }
        DATEOID => Field::new(elem_name, arrow::datatypes::DataType::Date32, true),
        TIMESTAMPOID => Field::new(
            elem_name,
            arrow::datatypes::DataType::Timestamp(arrow::datatypes::TimeUnit::Microsecond, None),
            true,
        ),
        TIMESTAMPTZOID => Field::new(
            elem_name,
            arrow::datatypes::DataType::Timestamp(
                arrow::datatypes::TimeUnit::Microsecond,
                Some("+00:00".into()),
            ),
            true,
        ),
        TIMEOID => Field::new(
            elem_name,
            arrow::datatypes::DataType::Time64(arrow::datatypes::TimeUnit::Microsecond),
            true,
        ),
        TIMETZOID => Field::new(
            elem_name,
            arrow::datatypes::DataType::Time64(arrow::datatypes::TimeUnit::Microsecond),
            true,
        )
        .with_metadata(HashMap::from_iter(vec![(
            "adjusted_to_utc".into(),
            "true".into(),
        )])),
        CHAROID => Field::new(elem_name, arrow::datatypes::DataType::Utf8, true),
        TEXTOID => Field::new(elem_name, arrow::datatypes::DataType::Utf8, true),
        BYTEAOID => Field::new(elem_name, arrow::datatypes::DataType::Binary, true),
        OIDOID => Field::new(elem_name, arrow::datatypes::DataType::UInt32, true),
        _ => {
            if is_postgis_geometry_type(typoid) {
                Field::new(elem_name, arrow::datatypes::DataType::Binary, true)
            } else {
                Field::new(elem_name, arrow::datatypes::DataType::Utf8, true)
            }
        }
    };

    // Combine the field metadata with the field metadata from the schema visitor
    let primitive_metadata = field
        .metadata()
        .iter()
        .chain(primitive_metadata.iter())
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();

    field.with_metadata(primitive_metadata).into()
}

fn adjust_map_entries_field(field: FieldRef) -> FieldRef {
    let name = field.deref().name();
    let data_type = field.deref().data_type();
    let metadata = field.deref().metadata().clone();

    let not_nullable_key_field;
    let nullable_value_field;

    match data_type {
        arrow::datatypes::DataType::Struct(fields) => {
            let key_field = fields.find("key").expect("expected key field").1;
            let value_field = fields.find("val").expect("expected val field").1;

            not_nullable_key_field =
                Field::new(key_field.name(), key_field.data_type().clone(), false)
                    .with_metadata(key_field.metadata().clone());

            nullable_value_field =
                Field::new(value_field.name(), value_field.data_type().clone(), true)
                    .with_metadata(value_field.metadata().clone());
        }
        _ => {
            panic!("expected struct data type for map entries")
        }
    };

    let entries_field = Field::new(
        name,
        arrow::datatypes::DataType::Struct(Fields::from(vec![
            not_nullable_key_field,
            nullable_value_field,
        ])),
        false,
    )
    .with_metadata(metadata);

    Arc::new(entries_field)
}

pub(crate) fn ensure_arrow_schema_match_tupledesc(
    file_schema: Arc<Schema>,
    tupledesc: &PgTupleDesc,
) {
    let table_schema = parse_arrow_schema_from_tupledesc(tupledesc);

    for table_schema_field in table_schema.fields().iter() {
        let table_schema_field_name = table_schema_field.name();
        let table_schema_field_type = table_schema_field.data_type();

        let file_schema_field = file_schema.column_with_name(table_schema_field_name);

        if let Some(file_schema_field) = file_schema_field {
            let file_schema_field_type = file_schema_field.1.data_type();

            if file_schema_field_type != table_schema_field_type {
                panic!(
                    "type mismatch for column \"{}\" between table and parquet file. table expected \"{}\" but file had \"{}\"",
                    table_schema_field_name,
                    table_schema_field_type,
                    file_schema_field_type,
                );
            }
        } else {
            panic!(
                "column \"{}\" is not found in parquet file",
                table_schema_field_name
            );
        }
    }
}
