use std::{
    collections::{HashMap, HashSet},
    ops::Deref,
    sync::Arc,
};

use arrow::datatypes::{Field, Fields, Schema};
use arrow_cast::can_cast_types;
use arrow_schema::{DataType, FieldRef};
use parquet::arrow::{ArrowSchemaConverter, PARQUET_FIELD_ID_META_KEY};
use pg_sys::{
    can_coerce_type,
    CoercionContext::{self, COERCION_EXPLICIT},
    FormData_pg_attribute, InvalidOid, Oid, BOOLOID, BYTEAOID, CHAROID, DATEOID, FLOAT4OID,
    FLOAT8OID, INT2OID, INT4OID, INT8OID, JSONBOID, JSONOID, NUMERICOID, OIDOID, TEXTOID, TIMEOID,
    TIMESTAMPOID, TIMESTAMPTZOID, TIMETZOID, UUIDOID,
};
use pgrx::{check_for_interrupts, prelude::*, PgTupleDesc};

use crate::{
    pgrx_utils::{
        array_element_typoid, collect_attributes_for, domain_array_base_elem_type, is_array_type,
        is_composite_type, is_generated_attribute, tuple_desc, CollectAttributesFor,
    },
    type_compat::{
        geometry::is_postgis_geometry_type,
        map::is_map_type,
        pg_arrow_type_conversions::{
            extract_precision_and_scale_from_numeric_typmod, should_write_numeric_as_text,
        },
    },
};

use super::{field_ids::FieldIds, match_by::MatchBy};

pub(crate) fn parquet_schema_string_from_attributes(
    attributes: &[FormData_pg_attribute],
    field_ids: FieldIds,
) -> String {
    let arrow_schema = parse_arrow_schema_from_attributes(attributes, field_ids);

    let parquet_schema = ArrowSchemaConverter::new()
        .convert(&arrow_schema)
        .unwrap_or_else(|e| panic!("failed to convert arrow schema to parquet schema: {}", e));

    let mut buf = Vec::new();
    parquet::schema::printer::print_schema(&mut buf, &parquet_schema.root_schema_ptr());
    String::from_utf8(buf).unwrap_or_else(|e| panic!("failed to convert schema to string: {}", e))
}

// FieldIdMappingContext is used to keep track of the current field path and field id
// while parsing the arrow schema from Postgres attributes.
//
// It is used to generate field ids for the fields in the arrow schema.
// current_field_path is used to get the field id from explicit field id mapping.
// current_field_id is used to generate field ids for fields when field ids are auto-generated.
// If the field ids are not requested, no field ids are generated.
struct FieldIdMappingContext {
    field_ids: FieldIds,
    current_field_id: i32,
    current_field_path: Vec<String>,
    assigned_field_ids: HashSet<i32>,
}

impl FieldIdMappingContext {
    fn new(field_ids: FieldIds) -> Self {
        Self {
            field_ids,
            current_field_id: 0,
            current_field_path: vec![],
            assigned_field_ids: HashSet::new(),
        }
    }

    fn add_field_name_to_path(&mut self, field_name: &str) {
        self.current_field_path.push(field_name.to_string());
    }

    fn remove_field_name_from_path(&mut self) {
        self.current_field_path.pop();
    }

    fn next_field_id(&mut self) -> Option<i32> {
        let field_id = match &self.field_ids {
            FieldIds::None => None,
            FieldIds::Explicit(field_id_mapping) => {
                field_id_mapping.field_id(&self.current_field_path)
            }
            FieldIds::Auto => {
                let field_id = self.current_field_id;
                self.current_field_id += 1;
                Some(field_id)
            }
        };

        if let Some(field_id) = field_id {
            // check for duplicate field id
            if self.assigned_field_ids.contains(&field_id) {
                panic!("duplicate field id {field_id} in \"field_ids\"");
            }

            self.assigned_field_ids.insert(field_id);
        }

        field_id
    }

    fn next_root_field_id(&mut self) -> Option<i32> {
        self.add_field_name_to_path("__root_field_id");

        let root_field_id = self.next_field_id();

        self.remove_field_name_from_path();

        root_field_id
    }

    fn field_with_id(&mut self, field: Field, field_id: Option<i32>) -> FieldRef {
        if let Some(field_id) = field_id {
            let field_id_metadata = HashMap::<String, String>::from_iter(vec![(
                PARQUET_FIELD_ID_META_KEY.into(),
                field_id.to_string(),
            )]);

            // append field id metadata to existing metadata of the field
            let metadata = field
                .metadata()
                .iter()
                .chain(field_id_metadata.iter())
                .map(|(k, v)| (k.clone(), v.clone()))
                .collect();

            field.with_metadata(metadata).into()
        } else {
            field.into()
        }
    }
}

pub(crate) fn parse_arrow_schema_from_attributes(
    attributes: &[FormData_pg_attribute],
    field_ids: FieldIds,
) -> Schema {
    let mut field_id_mapping_context = FieldIdMappingContext::new(field_ids);

    let mut struct_attribute_fields = vec![];

    for attribute in attributes {
        let attribute_name = attribute.name();
        let attribute_typoid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let field = if is_composite_type(attribute_typoid) {
            let attribute_tupledesc = tuple_desc(attribute_typoid, attribute_typmod);
            parse_struct_schema(
                attribute_tupledesc,
                attribute_name,
                &mut field_id_mapping_context,
            )
        } else if is_map_type(attribute_typoid) {
            let (entries_typoid, entries_typmod) = domain_array_base_elem_type(attribute_typoid);
            parse_map_schema(
                entries_typoid,
                entries_typmod,
                attribute_name,
                &mut field_id_mapping_context,
            )
        } else if is_array_type(attribute_typoid) {
            let attribute_element_typoid = array_element_typoid(attribute_typoid);
            parse_list_schema(
                attribute_element_typoid,
                attribute_typmod,
                attribute_name,
                &mut field_id_mapping_context,
            )
        } else {
            parse_primitive_schema(
                attribute_typoid,
                attribute_typmod,
                attribute_name,
                &mut field_id_mapping_context,
            )
        };

        struct_attribute_fields.push(field);
    }

    Schema::new(Fields::from(struct_attribute_fields))
}

fn parse_struct_schema(
    tupledesc: PgTupleDesc,
    struct_name: &str,
    field_id_mapping_context: &mut FieldIdMappingContext,
) -> Arc<Field> {
    check_for_interrupts!();

    field_id_mapping_context.add_field_name_to_path(struct_name);

    let struct_field_id = field_id_mapping_context.next_root_field_id();

    let mut child_fields: Vec<Arc<Field>> = vec![];

    let attributes = collect_attributes_for(CollectAttributesFor::Other, &tupledesc);

    for attribute in attributes {
        if attribute.is_dropped() {
            continue;
        }

        let attribute_name = attribute.name();
        let attribute_oid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let child_field = if is_composite_type(attribute_oid) {
            let attribute_tupledesc = tuple_desc(attribute_oid, attribute_typmod);
            parse_struct_schema(
                attribute_tupledesc,
                attribute_name,
                field_id_mapping_context,
            )
        } else if is_map_type(attribute_oid) {
            let (entries_typoid, entries_typmod) = domain_array_base_elem_type(attribute_oid);
            parse_map_schema(
                entries_typoid,
                entries_typmod,
                attribute_name,
                field_id_mapping_context,
            )
        } else if is_array_type(attribute_oid) {
            let attribute_element_typoid = array_element_typoid(attribute_oid);
            parse_list_schema(
                attribute_element_typoid,
                attribute_typmod,
                attribute_name,
                field_id_mapping_context,
            )
        } else {
            parse_primitive_schema(
                attribute_oid,
                attribute_typmod,
                attribute_name,
                field_id_mapping_context,
            )
        };

        child_fields.push(child_field);
    }

    field_id_mapping_context.remove_field_name_from_path();

    let nullable = true;

    let struct_field = Field::new(
        struct_name,
        arrow::datatypes::DataType::Struct(Fields::from(child_fields)),
        nullable,
    );

    field_id_mapping_context.field_with_id(struct_field, struct_field_id)
}

fn parse_list_schema(
    typoid: Oid,
    typmod: i32,
    array_name: &str,
    field_id_mapping_context: &mut FieldIdMappingContext,
) -> Arc<Field> {
    check_for_interrupts!();

    field_id_mapping_context.add_field_name_to_path(array_name);

    let list_field_id = field_id_mapping_context.next_root_field_id();

    let element_name = "element";

    let elem_field = if is_composite_type(typoid) {
        let tupledesc = tuple_desc(typoid, typmod);
        parse_struct_schema(tupledesc, element_name, field_id_mapping_context)
    } else if is_map_type(typoid) {
        let (entries_typoid, entries_typmod) = domain_array_base_elem_type(typoid);
        parse_map_schema(
            entries_typoid,
            entries_typmod,
            element_name,
            field_id_mapping_context,
        )
    } else {
        parse_primitive_schema(typoid, typmod, element_name, field_id_mapping_context)
    };

    field_id_mapping_context.remove_field_name_from_path();

    let nullable = true;

    let list_field = Field::new(
        array_name,
        arrow::datatypes::DataType::List(elem_field),
        nullable,
    );

    field_id_mapping_context.field_with_id(list_field, list_field_id)
}

fn parse_map_schema(
    typoid: Oid,
    typmod: i32,
    map_name: &str,
    field_id_mapping_context: &mut FieldIdMappingContext,
) -> Arc<Field> {
    let tupledesc = tuple_desc(typoid, typmod);

    let entries_field = parse_struct_schema(tupledesc, map_name, field_id_mapping_context);
    let entries_field = adjust_map_entries_field(entries_field);

    // map field id is the same as entries field id
    let map_field_id = entries_field
        .deref()
        .metadata()
        .get(PARQUET_FIELD_ID_META_KEY)
        .map(|v| {
            v.parse::<i32>()
                .expect("expected field id to be a valid integer")
        });

    let keys_sorted = false;

    let nullable = true;

    let map_field = Field::new(
        map_name,
        arrow::datatypes::DataType::Map(entries_field, keys_sorted),
        nullable,
    );

    field_id_mapping_context.field_with_id(map_field, map_field_id)
}

fn parse_primitive_schema(
    typoid: Oid,
    typmod: i32,
    scalar_name: &str,
    field_id_mapping_context: &mut FieldIdMappingContext,
) -> Arc<Field> {
    check_for_interrupts!();

    field_id_mapping_context.add_field_name_to_path(scalar_name);

    let primitive_field_id = field_id_mapping_context.next_field_id();

    let nullable = true;

    let field = match typoid {
        FLOAT4OID => Field::new(scalar_name, arrow::datatypes::DataType::Float32, nullable),
        FLOAT8OID => Field::new(scalar_name, arrow::datatypes::DataType::Float64, nullable),
        BOOLOID => Field::new(scalar_name, arrow::datatypes::DataType::Boolean, nullable),
        INT2OID => Field::new(scalar_name, arrow::datatypes::DataType::Int16, nullable),
        INT4OID => Field::new(scalar_name, arrow::datatypes::DataType::Int32, nullable),
        INT8OID => Field::new(scalar_name, arrow::datatypes::DataType::Int64, nullable),
        UUIDOID => Field::new(
            scalar_name,
            arrow::datatypes::DataType::FixedSizeBinary(16),
            nullable,
        )
        .with_extension_type(arrow_schema::extension::Uuid),
        NUMERICOID => {
            let (precision, scale) = extract_precision_and_scale_from_numeric_typmod(typmod);

            if should_write_numeric_as_text(precision) {
                Field::new(scalar_name, arrow::datatypes::DataType::Utf8, nullable)
            } else {
                Field::new(
                    scalar_name,
                    arrow::datatypes::DataType::Decimal128(precision as _, scale as _),
                    nullable,
                )
            }
        }
        DATEOID => Field::new(scalar_name, arrow::datatypes::DataType::Date32, nullable),
        TIMESTAMPOID => Field::new(
            scalar_name,
            arrow::datatypes::DataType::Timestamp(arrow::datatypes::TimeUnit::Microsecond, None),
            nullable,
        ),
        TIMESTAMPTZOID => Field::new(
            scalar_name,
            arrow::datatypes::DataType::Timestamp(
                arrow::datatypes::TimeUnit::Microsecond,
                Some("+00:00".into()),
            ),
            nullable,
        ),
        TIMEOID => Field::new(
            scalar_name,
            arrow::datatypes::DataType::Time64(arrow::datatypes::TimeUnit::Microsecond),
            nullable,
        ),
        TIMETZOID => Field::new(
            scalar_name,
            arrow::datatypes::DataType::Time64(arrow::datatypes::TimeUnit::Microsecond),
            nullable,
        )
        .with_metadata(HashMap::from_iter(vec![(
            "adjusted_to_utc".into(),
            "true".into(),
        )])),
        CHAROID => Field::new(scalar_name, arrow::datatypes::DataType::Utf8, nullable),
        TEXTOID => Field::new(scalar_name, arrow::datatypes::DataType::Utf8, nullable),
        JSONOID | JSONBOID => Field::new(scalar_name, arrow::datatypes::DataType::Utf8, nullable)
            .with_extension_type(arrow_schema::extension::Json::default()),
        BYTEAOID => Field::new(scalar_name, arrow::datatypes::DataType::Binary, nullable),
        OIDOID => Field::new(scalar_name, arrow::datatypes::DataType::UInt32, nullable),
        _ => {
            if is_postgis_geometry_type(typoid) {
                Field::new(scalar_name, arrow::datatypes::DataType::Binary, nullable)
            } else {
                Field::new(scalar_name, arrow::datatypes::DataType::Utf8, nullable)
            }
        }
    };

    field_id_mapping_context.remove_field_name_from_path();

    field_id_mapping_context.field_with_id(field, primitive_field_id)
}

fn adjust_map_entries_field(field: FieldRef) -> FieldRef {
    let not_nullable_key_field;
    let nullable_value_field;

    match field.deref().data_type() {
        arrow::datatypes::DataType::Struct(fields) => {
            let key_field = fields.find("key").expect("expected key field").1;
            let value_field = fields.find("val").expect("expected val field").1;

            let key_nullable = false;

            not_nullable_key_field = Field::new(
                key_field.name(),
                key_field.data_type().clone(),
                key_nullable,
            )
            .with_metadata(key_field.metadata().clone());

            let value_nullable = true;

            nullable_value_field = Field::new(
                value_field.name(),
                value_field.data_type().clone(),
                value_nullable,
            )
            .with_metadata(value_field.metadata().clone());
        }
        _ => {
            panic!("expected struct data type for map key_value field")
        }
    };

    let entries_nullable = false;

    let entries_name = "key_value";

    let metadata = field.deref().metadata().clone();

    let entries_field = Field::new(
        entries_name,
        arrow::datatypes::DataType::Struct(Fields::from(vec![
            not_nullable_key_field,
            nullable_value_field,
        ])),
        entries_nullable,
    )
    .with_metadata(metadata);

    Arc::new(entries_field)
}

pub(crate) fn error_if_copy_from_match_by_position_with_generated_columns(
    tupledesc: &PgTupleDesc,
    match_by: MatchBy,
) {
    // match_by 'name' can handle generated columns
    if let MatchBy::Name = match_by {
        return;
    }

    let attributes = collect_attributes_for(CollectAttributesFor::Other, tupledesc);

    for attribute in attributes {
        if is_generated_attribute(&attribute) {
            ereport!(
                PgLogLevel::ERROR,
                PgSqlErrorCode::ERRCODE_FEATURE_NOT_SUPPORTED,
                "COPY FROM parquet with generated columns is not supported",
                "Try COPY FROM parquet WITH (match_by 'name'). \"
                 It works only if the column names match with parquet file's.",
            );
        }
    }
}

// ensure_file_schema_match_tupledesc_schema throws an error if the file's schema does not match the table schema.
// If the file's arrow schema is castable to the table's arrow schema, it returns a vector of Option<DataType>
// to cast to for each field.
pub(crate) fn ensure_file_schema_match_tupledesc_schema(
    file_schema: Arc<Schema>,
    tupledesc_schema: Arc<Schema>,
    attributes: &[FormData_pg_attribute],
    match_by: MatchBy,
) -> Vec<Option<DataType>> {
    let mut cast_to_types = Vec::new();

    if match_by == MatchBy::Position
        && tupledesc_schema.fields().len() != file_schema.fields().len()
    {
        panic!(
            "column count mismatch between table and parquet file. \
             parquet file has {} columns, but table has {} columns",
            file_schema.fields().len(),
            tupledesc_schema.fields().len()
        );
    }

    for (tupledesc_schema_field, attribute) in
        tupledesc_schema.fields().iter().zip(attributes.iter())
    {
        let field_name = tupledesc_schema_field.name();

        let file_schema_field = match match_by {
            MatchBy::Position => file_schema.field(attribute.attnum as usize - 1),

            MatchBy::Name => {
                let file_schema_field = file_schema.column_with_name(field_name);

                if file_schema_field.is_none() {
                    panic!("column \"{}\" is not found in parquet file", field_name);
                }

                let (_, file_schema_field) = file_schema_field.unwrap();

                file_schema_field
            }
        };

        let file_schema_field = Arc::new(file_schema_field.clone());

        let from_type = file_schema_field.data_type();
        let to_type = tupledesc_schema_field.data_type();

        // no cast needed
        if from_type == to_type {
            cast_to_types.push(None);
            continue;
        }

        if !is_coercible(
            &file_schema_field,
            tupledesc_schema_field,
            attribute.atttypid,
            attribute.atttypmod,
        ) {
            panic!(
                "type mismatch for column \"{}\" between table and parquet file.\n\n\
                 table has \"{}\"\n\nparquet file has \"{}\"",
                field_name, to_type, from_type
            );
        }

        pgrx::debug2!(
            "column \"{}\" is being cast from \"{}\" to \"{}\"",
            field_name,
            from_type,
            to_type
        );

        cast_to_types.push(Some(to_type.clone()));
    }

    cast_to_types
}

// is_coercible first checks if "from_type" can be cast to "to_type" by arrow-cast.
// Then, it checks if the cast is meaningful at Postgres by seeing if there is
// an explicit coercion from "from_typoid" to "to_typoid".
//
// Additionaly, we need to be careful about struct rules for the cast:
// Arrow supports casting struct fields by field position instead of field name,
// which is not the intended behavior for pg_parquet. Hence, we make sure the field names
// match for structs.
fn is_coercible(
    from_field: &FieldRef,
    to_field: &FieldRef,
    to_typoid: Oid,
    to_typmod: i32,
) -> bool {
    match (from_field.data_type(), to_field.data_type()) {
        (DataType::Struct(from_fields), DataType::Struct(to_fields)) => {
            if from_fields.len() != to_fields.len() {
                return false;
            }

            let tupledesc = tuple_desc(to_typoid, to_typmod);

            let attributes = collect_attributes_for(CollectAttributesFor::Other, &tupledesc);

            for (from_field, (to_field, to_attribute)) in from_fields
                .iter()
                .zip(to_fields.iter().zip(attributes.iter()))
            {
                if from_field.name() != to_field.name() {
                    return false;
                }

                if !is_coercible(
                    from_field,
                    to_field,
                    to_attribute.type_oid().value(),
                    to_attribute.type_mod(),
                ) {
                    return false;
                }
            }

            true
        }
        (DataType::List(from_field), DataType::List(to_field))
        | (DataType::FixedSizeList(from_field, _), DataType::List(to_field))
        | (DataType::LargeList(from_field), DataType::List(to_field)) => {
            let element_oid = array_element_typoid(to_typoid);
            let element_typmod = to_typmod;

            is_coercible(from_field, to_field, element_oid, element_typmod)
        }
        (DataType::Map(from_entries_field, _), DataType::Map(to_entries_field, _)) => {
            // entries field cannot be null
            if from_entries_field.is_nullable() {
                return false;
            }

            let (entries_typoid, entries_typmod) = domain_array_base_elem_type(to_typoid);

            is_coercible(
                from_entries_field,
                to_entries_field,
                entries_typoid,
                entries_typmod,
            )
        }
        _ => {
            // check if arrow-cast can cast the types
            if !can_cast_types(from_field.data_type(), to_field.data_type()) {
                return false;
            }

            let from_typoid = pg_type_for_arrow_primitive_field(from_field);

            // pg_parquet could not recognize that arrow type
            if from_typoid == InvalidOid {
                return false;
            }

            // check if coercion is meaningful at Postgres (it has a coercion path)
            can_pg_coerce_types(from_typoid, to_typoid, COERCION_EXPLICIT)
        }
    }
}

fn can_pg_coerce_types(from_typoid: Oid, to_typoid: Oid, ccontext: CoercionContext::Type) -> bool {
    let n_args = 1;
    let input_typeids = [from_typoid];
    let target_typeids = [to_typoid];

    unsafe {
        can_coerce_type(
            n_args,
            input_typeids.as_ptr(),
            target_typeids.as_ptr(),
            ccontext,
        )
    }
}

// pg_type_for_arrow_primitive_field returns Postgres type for given
// primitive arrow field. It returns InvalidOid if the arrow field's type is not recognized.
fn pg_type_for_arrow_primitive_field(field: &FieldRef) -> Oid {
    match field.data_type() {
        DataType::Float32 | DataType::Float16 => FLOAT4OID,
        DataType::Float64 => FLOAT8OID,
        DataType::Int16 | DataType::UInt16 | DataType::Int8 | DataType::UInt8 => INT2OID,
        DataType::Int32 | DataType::UInt32 => INT4OID,
        DataType::Int64 | DataType::UInt64 => INT8OID,
        DataType::Decimal128(_, _) => NUMERICOID,
        DataType::Boolean => BOOLOID,
        DataType::Date32 => DATEOID,
        DataType::Time64(_) => TIMEOID,
        DataType::Timestamp(_, None) => TIMESTAMPOID,
        DataType::Timestamp(_, Some(_)) => TIMESTAMPTZOID,
        DataType::Utf8 | DataType::LargeUtf8 if field.extension_type_name().is_none() => TEXTOID,
        DataType::Utf8 | DataType::LargeUtf8
            if field
                .try_extension_type::<arrow_schema::extension::Json>()
                .is_ok() =>
        {
            JSONOID
        }
        DataType::Binary | DataType::LargeBinary => BYTEAOID,
        DataType::FixedSizeBinary(16)
            if field
                .try_extension_type::<arrow_schema::extension::Uuid>()
                .is_ok() =>
        {
            UUIDOID
        }
        _ => InvalidOid,
    }
}
