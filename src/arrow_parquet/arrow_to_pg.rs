use arrow::array::{
    Array, ArrayData, BinaryArray, BooleanArray, Date32Array, Decimal128Array, Float32Array,
    Float64Array, Int16Array, Int32Array, Int64Array, ListArray, MapArray, StringArray,
    StructArray, Time64MicrosecondArray, TimestampMicrosecondArray, UInt32Array,
};
use arrow_schema::{DataType, FieldRef, Fields, TimeUnit};
use pgrx::{
    datum::{Date, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone},
    pg_sys::{Datum, FormData_pg_attribute, Oid, CHAROID, TEXTOID, TIMEOID},
    prelude::PgHeapTuple,
    AllocatedByRust, AnyNumeric, IntoDatum, PgTupleDesc,
};

use crate::{
    pgrx_utils::{
        array_element_typoid, collect_attributes_for, domain_array_base_elem_typoid, is_array_type,
        is_composite_type, tuple_desc, CollectAttributesFor,
    },
    type_compat::{
        fallback_to_text::{reset_fallback_to_text_context, FallbackToText},
        geometry::{is_postgis_geometry_type, Geometry},
        map::{is_map_type, Map},
    },
};

pub(crate) mod bool;
pub(crate) mod bytea;
pub(crate) mod char;
pub(crate) mod composite;
pub(crate) mod date;
pub(crate) mod fallback_to_text;
pub(crate) mod float4;
pub(crate) mod float8;
pub(crate) mod geometry;
pub(crate) mod int2;
pub(crate) mod int4;
pub(crate) mod int8;
pub(crate) mod map;
pub(crate) mod numeric;
pub(crate) mod oid;
pub(crate) mod text;
pub(crate) mod time;
pub(crate) mod timestamp;
pub(crate) mod timestamptz;
pub(crate) mod timetz;

pub(crate) trait ArrowArrayToPgType<T: IntoDatum>: From<ArrayData> {
    fn to_pg_type(self, context: &ArrowToPgAttributeContext) -> Option<T>;
}

#[derive(Clone)]
pub(crate) struct ArrowToPgAttributeContext {
    name: String,
    data_type: DataType,
    needs_cast: bool,
    typoid: Oid,
    typmod: i32,
    is_geometry: bool,
    attribute_contexts: Option<Vec<ArrowToPgAttributeContext>>,
    attribute_tupledesc: Option<PgTupleDesc<'static>>,
    precision: Option<u32>,
    scale: Option<u32>,
    timezone: Option<String>,
}

impl ArrowToPgAttributeContext {
    pub(crate) fn new(
        name: &str,
        typoid: Oid,
        typmod: i32,
        field: FieldRef,
        cast_to_type: Option<DataType>,
    ) -> Self {
        let needs_cast = cast_to_type.is_some();

        let data_type = if let Some(cast_to_type) = &cast_to_type {
            cast_to_type.clone()
        } else {
            field.data_type().clone()
        };

        let is_array = is_array_type(typoid);
        let is_composite;
        let is_geometry;
        let is_map;
        let attribute_typoid;
        let attribute_field;

        if is_array {
            let element_typoid = array_element_typoid(typoid);

            is_composite = is_composite_type(element_typoid);
            is_geometry = is_postgis_geometry_type(element_typoid);
            is_map = is_map_type(element_typoid);

            if is_map {
                let entries_typoid = domain_array_base_elem_typoid(element_typoid);
                attribute_typoid = entries_typoid;
            } else {
                attribute_typoid = element_typoid;
            }

            attribute_field = match field.data_type() {
                arrow::datatypes::DataType::List(field) => field.clone(),
                _ => unreachable!(),
            }
        } else {
            is_composite = is_composite_type(typoid);
            is_geometry = is_postgis_geometry_type(typoid);
            is_map = is_map_type(typoid);

            if is_map {
                let entries_typoid = domain_array_base_elem_typoid(typoid);
                attribute_typoid = entries_typoid;
            } else {
                attribute_typoid = typoid;
            }

            attribute_field = field.clone();
        }

        let attribute_tupledesc = if is_composite || is_map {
            Some(tuple_desc(attribute_typoid, typmod))
        } else {
            None
        };

        let (precision, scale) = match &data_type {
            DataType::Decimal128(p, s) => (Some(*p as _), Some(*s as _)),
            DataType::List(field) => {
                if let DataType::Decimal128(p, s) = field.data_type() {
                    (Some(*p as _), Some(*s as _))
                } else {
                    (None, None)
                }
            }
            _ => (None, None),
        };

        let timezone = match &data_type {
            DataType::Timestamp(_, Some(timezone)) => Some(timezone.to_string()),
            DataType::List(field) => {
                if let DataType::Timestamp(_, Some(timezone)) = field.data_type() {
                    Some(timezone.to_string())
                } else {
                    None
                }
            }
            _ => None,
        };

        // for composite and map types, recursively collect attribute contexts
        let attribute_contexts = if let Some(attribute_tupledesc) = &attribute_tupledesc {
            let fields = match attribute_field.data_type() {
                arrow::datatypes::DataType::Struct(fields) => fields.clone(),
                arrow::datatypes::DataType::Map(struct_field, _) => {
                    match struct_field.data_type() {
                        arrow::datatypes::DataType::Struct(fields) => fields.clone(),
                        _ => unreachable!(),
                    }
                }
                _ => unreachable!(),
            };

            let attributes =
                collect_attributes_for(CollectAttributesFor::Struct, attribute_tupledesc);

            // we only cast the top-level attributes, which already covers the nested attributes
            let cast_to_types = None;

            Some(collect_arrow_to_pg_attribute_contexts(
                &attributes,
                &fields,
                cast_to_types,
            ))
        } else {
            None
        };

        Self {
            name: name.to_string(),
            data_type,
            needs_cast,
            typoid: attribute_typoid,
            typmod,
            is_geometry,
            attribute_contexts,
            attribute_tupledesc,
            scale,
            precision,
            timezone,
        }
    }

    pub(crate) fn name(&self) -> &str {
        &self.name
    }

    pub(crate) fn needs_cast(&self) -> bool {
        self.needs_cast
    }

    pub(crate) fn data_type(&self) -> &DataType {
        &self.data_type
    }
}

pub(crate) fn collect_arrow_to_pg_attribute_contexts(
    attributes: &[FormData_pg_attribute],
    fields: &Fields,
    cast_to_types: Option<Vec<Option<DataType>>>,
) -> Vec<ArrowToPgAttributeContext> {
    let mut attribute_contexts = vec![];

    for (idx, attribute) in attributes.iter().enumerate() {
        let attribute_name = attribute.name();
        let attribute_typoid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let field = fields
            .iter()
            .find(|field| field.name() == attribute_name)
            .unwrap_or_else(|| panic!("failed to find field {}", attribute_name))
            .clone();

        let cast_to_type = if let Some(cast_to_types) = cast_to_types.as_ref() {
            debug_assert!(cast_to_types.len() == attributes.len());
            cast_to_types.get(idx).cloned().expect("cast_to_type null")
        } else {
            None
        };

        let attribute_context = ArrowToPgAttributeContext::new(
            attribute_name,
            attribute_typoid,
            attribute_typmod,
            field,
            cast_to_type,
        );

        attribute_contexts.push(attribute_context);
    }

    attribute_contexts
}

pub(crate) fn to_pg_datum(
    attribute_array: ArrayData,
    attribute_context: &ArrowToPgAttributeContext,
) -> Option<Datum> {
    if matches!(attribute_array.data_type(), DataType::List(_)) {
        to_pg_array_datum(attribute_array, attribute_context)
    } else {
        to_pg_nonarray_datum(attribute_array, attribute_context)
    }
}

macro_rules! to_pg_datum {
    ($arrow_array_type:ty, $pg_type:ty, $arrow_array:expr, $attribute_context:expr) => {{
        let arrow_array: $arrow_array_type = $arrow_array.into();

        let val: Option<$pg_type> = arrow_array.to_pg_type($attribute_context);

        val.into_datum()
    }};
}

fn to_pg_nonarray_datum(
    primitive_array: ArrayData,
    attribute_context: &ArrowToPgAttributeContext,
) -> Option<Datum> {
    match attribute_context.data_type() {
        DataType::Float32 => {
            to_pg_datum!(Float32Array, f32, primitive_array, attribute_context)
        }
        DataType::Float64 => {
            to_pg_datum!(Float64Array, f64, primitive_array, attribute_context)
        }
        DataType::Int16 => {
            to_pg_datum!(Int16Array, i16, primitive_array, attribute_context)
        }
        DataType::Int32 => {
            to_pg_datum!(Int32Array, i32, primitive_array, attribute_context)
        }
        DataType::Int64 => {
            to_pg_datum!(Int64Array, i64, primitive_array, attribute_context)
        }
        DataType::UInt32 => {
            to_pg_datum!(UInt32Array, Oid, primitive_array, attribute_context)
        }
        DataType::Boolean => {
            to_pg_datum!(BooleanArray, bool, primitive_array, attribute_context)
        }
        DataType::Utf8 => {
            if attribute_context.typoid == CHAROID {
                to_pg_datum!(StringArray, i8, primitive_array, attribute_context)
            } else if attribute_context.typoid == TEXTOID {
                to_pg_datum!(StringArray, String, primitive_array, attribute_context)
            } else {
                reset_fallback_to_text_context(attribute_context.typoid, attribute_context.typmod);

                to_pg_datum!(
                    StringArray,
                    FallbackToText,
                    primitive_array,
                    attribute_context
                )
            }
        }
        DataType::Binary => {
            if attribute_context.is_geometry {
                to_pg_datum!(BinaryArray, Geometry, primitive_array, attribute_context)
            } else {
                to_pg_datum!(BinaryArray, Vec<u8>, primitive_array, attribute_context)
            }
        }
        DataType::Decimal128(_, _) => {
            to_pg_datum!(
                Decimal128Array,
                AnyNumeric,
                primitive_array,
                attribute_context
            )
        }
        DataType::Date32 => {
            to_pg_datum!(Date32Array, Date, primitive_array, attribute_context)
        }
        DataType::Time64(TimeUnit::Microsecond) => {
            if attribute_context.typoid == TIMEOID {
                to_pg_datum!(
                    Time64MicrosecondArray,
                    Time,
                    primitive_array,
                    attribute_context
                )
            } else {
                to_pg_datum!(
                    Time64MicrosecondArray,
                    TimeWithTimeZone,
                    primitive_array,
                    attribute_context
                )
            }
        }
        DataType::Timestamp(TimeUnit::Microsecond, None) => {
            to_pg_datum!(
                TimestampMicrosecondArray,
                Timestamp,
                primitive_array,
                attribute_context
            )
        }
        DataType::Timestamp(TimeUnit::Microsecond, Some(_)) => {
            to_pg_datum!(
                TimestampMicrosecondArray,
                TimestampWithTimeZone,
                primitive_array,
                attribute_context
            )
        }
        DataType::Struct(_) => {
            to_pg_datum!(
                StructArray,
                PgHeapTuple<AllocatedByRust>,
                primitive_array,
                attribute_context
            )
        }
        DataType::Map(_, _) => {
            to_pg_datum!(MapArray, Map, primitive_array, attribute_context)
        }
        _ => {
            panic!("unsupported data type: {:?}", attribute_context.data_type());
        }
    }
}

fn to_pg_array_datum(
    list_array: ArrayData,
    attribute_context: &ArrowToPgAttributeContext,
) -> Option<Datum> {
    let list_array: ListArray = list_array.into();

    if list_array.is_null(0) {
        return None;
    }

    let list_array = list_array.value(0).to_data();

    let element_field = match attribute_context.data_type() {
        DataType::List(field) => field,
        _ => unreachable!(),
    };

    match element_field.data_type() {
        DataType::Float32 => {
            to_pg_datum!(
                Float32Array,
                Vec<Option<f32>>,
                list_array,
                attribute_context
            )
        }
        DataType::Float64 => {
            to_pg_datum!(
                Float64Array,
                Vec<Option<f64>>,
                list_array,
                attribute_context
            )
        }
        DataType::Int16 => {
            to_pg_datum!(Int16Array, Vec<Option<i16>>, list_array, attribute_context)
        }
        DataType::Int32 => {
            to_pg_datum!(Int32Array, Vec<Option<i32>>, list_array, attribute_context)
        }
        DataType::Int64 => {
            to_pg_datum!(Int64Array, Vec<Option<i64>>, list_array, attribute_context)
        }
        DataType::UInt32 => {
            to_pg_datum!(UInt32Array, Vec<Option<Oid>>, list_array, attribute_context)
        }
        DataType::Boolean => {
            to_pg_datum!(
                BooleanArray,
                Vec<Option<bool>>,
                list_array,
                attribute_context
            )
        }
        DataType::Utf8 => {
            if attribute_context.typoid == CHAROID {
                to_pg_datum!(StringArray, Vec<Option<i8>>, list_array, attribute_context)
            } else if attribute_context.typoid == TEXTOID {
                to_pg_datum!(
                    StringArray,
                    Vec<Option<String>>,
                    list_array,
                    attribute_context
                )
            } else {
                reset_fallback_to_text_context(attribute_context.typoid, attribute_context.typmod);

                to_pg_datum!(
                    StringArray,
                    Vec<Option<FallbackToText>>,
                    list_array,
                    attribute_context
                )
            }
        }
        DataType::Binary => {
            if attribute_context.is_geometry {
                to_pg_datum!(
                    BinaryArray,
                    Vec<Option<Geometry>>,
                    list_array,
                    attribute_context
                )
            } else {
                to_pg_datum!(
                    BinaryArray,
                    Vec<Option<Vec<u8>>>,
                    list_array,
                    attribute_context
                )
            }
        }
        DataType::Decimal128(_, _) => {
            to_pg_datum!(
                Decimal128Array,
                Vec<Option<AnyNumeric>>,
                list_array,
                attribute_context
            )
        }
        DataType::Date32 => {
            to_pg_datum!(
                Date32Array,
                Vec<Option<Date>>,
                list_array,
                attribute_context
            )
        }
        DataType::Time64(TimeUnit::Microsecond) => {
            if attribute_context.typoid == TIMEOID {
                to_pg_datum!(
                    Time64MicrosecondArray,
                    Vec<Option<Time>>,
                    list_array,
                    attribute_context
                )
            } else {
                to_pg_datum!(
                    Time64MicrosecondArray,
                    Vec<Option<TimeWithTimeZone>>,
                    list_array,
                    attribute_context
                )
            }
        }
        DataType::Timestamp(TimeUnit::Microsecond, None) => {
            to_pg_datum!(
                TimestampMicrosecondArray,
                Vec<Option<Timestamp>>,
                list_array,
                attribute_context
            )
        }
        DataType::Timestamp(TimeUnit::Microsecond, Some(_)) => {
            to_pg_datum!(
                TimestampMicrosecondArray,
                Vec<Option<TimestampWithTimeZone>>,
                list_array,
                attribute_context
            )
        }
        DataType::Struct(_) => {
            to_pg_datum!(
                StructArray,
                Vec<Option<PgHeapTuple<AllocatedByRust>>>,
                list_array,
                attribute_context
            )
        }
        DataType::Map(_, _) => {
            to_pg_datum!(MapArray, Vec<Option<Map>>, list_array, attribute_context)
        }
        _ => {
            panic!("unsupported data type: {:?}", attribute_context.data_type());
        }
    }
}
