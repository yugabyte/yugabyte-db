use std::num::NonZeroUsize;

use arrow::array::ArrayRef;
use context::PgToArrowAttributeContext;
use pgrx::{
    check_for_interrupts,
    datum::{Date, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone, UnboxDatum},
    heap_tuple::PgHeapTuple,
    pg_sys::{
        Oid, BOOLOID, BYTEAOID, CHAROID, DATEOID, FLOAT4OID, FLOAT8OID, INT2OID, INT4OID, INT8OID,
        JSONBOID, JSONOID, NUMERICOID, OIDOID, TEXTOID, TIMEOID, TIMESTAMPOID, TIMESTAMPTZOID,
        TIMETZOID, UUIDOID,
    },
    AllocatedByRust, AnyNumeric, FromDatum, Json, JsonB, Uuid,
};

use crate::{
    pgrx_utils::{
        array_element_typoid, collect_attributes_for, domain_array_base_elem_type, is_array_type,
        is_composite_type, tuple_desc, CollectAttributesFor,
    },
    type_compat::{
        fallback_to_text::{reset_fallback_to_text_context, FallbackToText},
        geometry::{is_postgis_geometry_type, Geometry},
        map::{is_map_type, reset_map_type_context, Map},
        pg_arrow_type_conversions::{
            extract_precision_and_scale_from_numeric_typmod, should_write_numeric_as_text,
        },
    },
};

pub(crate) mod bool;
pub(crate) mod bytea;
pub(crate) mod char;
pub(crate) mod composite;
pub(crate) mod context;
pub(crate) mod date;
pub(crate) mod fallback_to_text;
pub(crate) mod float4;
pub(crate) mod float8;
pub(crate) mod geometry;
pub(crate) mod int2;
pub(crate) mod int4;
pub(crate) mod int8;
pub(crate) mod json;
pub(crate) mod jsonb;
pub(crate) mod map;
pub(crate) mod numeric;
pub(crate) mod oid;
pub(crate) mod text;
pub(crate) mod time;
pub(crate) mod timestamp;
pub(crate) mod timestamptz;
pub(crate) mod timetz;
pub(crate) mod uuid;

pub(crate) trait PgTypeToArrowArray<T: FromDatum + UnboxDatum> {
    fn to_arrow_array(self, context: &PgToArrowAttributeContext) -> ArrayRef;
}

pub(crate) fn to_arrow_array(
    tuples: &Vec<Option<PgHeapTuple<AllocatedByRust>>>,
    attribute_context: &PgToArrowAttributeContext,
) -> ArrayRef {
    if attribute_context.is_array() {
        to_arrow_list_array(tuples, attribute_context)
    } else {
        to_arrow_primitive_array(tuples, attribute_context)
    }
}

macro_rules! to_arrow_primitive_array {
    ($pg_type:ty, $tuples:expr, $attribute_context:expr) => {{
        let mut attribute_vals = vec![];

        for tuple in $tuples {
            check_for_interrupts!();

            if let Some(tuple) = tuple {
                let attribute_val: Option<$pg_type> = tuple
                    .get_by_index(
                        NonZeroUsize::new($attribute_context.attnum() as usize)
                            .expect("invalid attnum"),
                    )
                    .unwrap_or_else(|e| panic!("failed to get attribute: {}", e));

                attribute_vals.push(attribute_val);
            } else {
                attribute_vals.push(None);
            }
        }

        return attribute_vals.to_arrow_array($attribute_context);
    }};
}

macro_rules! to_arrow_list_array {
    ($pg_type:ty, $tuples:expr, $attribute_context:expr) => {{
        let mut attribute_vals = vec![];

        for tuple in $tuples {
            check_for_interrupts!();

            if let Some(tuple) = tuple {
                let attribute_val: Option<$pg_type> = tuple
                    .get_by_index(
                        NonZeroUsize::new($attribute_context.attnum() as usize)
                            .expect("invalid attnum"),
                    )
                    .unwrap_or_else(|e| panic!("failed to get attribute: {}", e));

                attribute_vals.push(attribute_val);
            } else {
                attribute_vals.push(None);
            }
        }

        let attribute_vals = attribute_vals
            .iter()
            .map(|val| val.as_ref().map(|val| val.iter().collect::<Vec<_>>()))
            .collect::<Vec<_>>();

        return attribute_vals.to_arrow_array($attribute_context);
    }};
}

fn to_arrow_primitive_array(
    tuples: &Vec<Option<PgHeapTuple<AllocatedByRust>>>,
    attribute_context: &PgToArrowAttributeContext,
) -> ArrayRef {
    match attribute_context.typoid() {
        FLOAT4OID => to_arrow_primitive_array!(f32, tuples, attribute_context),
        FLOAT8OID => to_arrow_primitive_array!(f64, tuples, attribute_context),
        INT2OID => to_arrow_primitive_array!(i16, tuples, attribute_context),
        INT4OID => to_arrow_primitive_array!(i32, tuples, attribute_context),
        INT8OID => to_arrow_primitive_array!(i64, tuples, attribute_context),
        UUIDOID => to_arrow_primitive_array!(Uuid, tuples, attribute_context),
        NUMERICOID => {
            let precision = attribute_context.precision();

            if should_write_numeric_as_text(precision) {
                reset_fallback_to_text_context(
                    attribute_context.typoid(),
                    attribute_context.typmod(),
                );

                to_arrow_primitive_array!(FallbackToText, tuples, attribute_context)
            } else {
                to_arrow_primitive_array!(AnyNumeric, tuples, attribute_context)
            }
        }
        BOOLOID => to_arrow_primitive_array!(bool, tuples, attribute_context),
        DATEOID => to_arrow_primitive_array!(Date, tuples, attribute_context),
        TIMEOID => to_arrow_primitive_array!(Time, tuples, attribute_context),
        TIMETZOID => to_arrow_primitive_array!(TimeWithTimeZone, tuples, attribute_context),
        TIMESTAMPOID => to_arrow_primitive_array!(Timestamp, tuples, attribute_context),
        TIMESTAMPTZOID => {
            to_arrow_primitive_array!(TimestampWithTimeZone, tuples, attribute_context)
        }
        CHAROID => to_arrow_primitive_array!(i8, tuples, attribute_context),
        TEXTOID => to_arrow_primitive_array!(String, tuples, attribute_context),
        JSONOID => to_arrow_primitive_array!(Json, tuples, attribute_context),
        JSONBOID => to_arrow_primitive_array!(JsonB, tuples, attribute_context),
        BYTEAOID => to_arrow_primitive_array!(&[u8], tuples, attribute_context),
        OIDOID => to_arrow_primitive_array!(Oid, tuples, attribute_context),
        _ => {
            if attribute_context.is_composite() {
                let mut attribute_vals = vec![];

                let attribute_tupledesc = attribute_context.tupledesc();

                for tuple in tuples {
                    check_for_interrupts!();

                    if let Some(tuple) = tuple {
                        let attribute_val: Option<PgHeapTuple<AllocatedByRust>> = tuple
                            .get_by_index(
                                NonZeroUsize::new(attribute_context.attnum() as usize)
                                    .expect("invalid attnum"),
                            )
                            .unwrap_or_else(|e| panic!("failed to get attribute: {}", e));

                        // this trick is needed to avoid having a bunch of
                        // reference counted tupledesc which comes from pgrx's "get_by_name".
                        // we first convert PgHeapTuple into unsafe HeapTuple to drop
                        // the reference counted tupledesc and then convert it back to
                        // PgHeapTuple by reusing the same tupledesc that we created
                        // before the loop. Only overhead is 1 "heap_copy_tuple" call.
                        let attribute_val = attribute_val.map(|tuple| tuple.into_pg());
                        let attribute_val = attribute_val.map(|tuple| unsafe {
                            PgHeapTuple::from_heap_tuple(attribute_tupledesc.clone(), tuple)
                                .into_owned()
                        });

                        attribute_vals.push(attribute_val);
                    } else {
                        attribute_vals.push(None);
                    }
                }

                attribute_vals.to_arrow_array(attribute_context)
            } else if attribute_context.is_map() {
                reset_map_type_context(attribute_context.typoid());

                to_arrow_primitive_array!(Map, tuples, attribute_context)
            } else if attribute_context.is_geometry() {
                to_arrow_primitive_array!(Geometry, tuples, attribute_context)
            } else {
                reset_fallback_to_text_context(
                    attribute_context.typoid(),
                    attribute_context.typmod(),
                );

                to_arrow_primitive_array!(FallbackToText, tuples, attribute_context)
            }
        }
    }
}

fn to_arrow_list_array(
    tuples: &Vec<Option<PgHeapTuple<AllocatedByRust>>>,
    attribute_context: &PgToArrowAttributeContext,
) -> ArrayRef {
    let element_context = attribute_context.element_context();
    let element_typoid = element_context.typoid();
    let element_typmod = element_context.typmod();

    match element_typoid {
        FLOAT4OID => to_arrow_list_array!(pgrx::Array<f32>, tuples, element_context),
        FLOAT8OID => to_arrow_list_array!(pgrx::Array<f64>, tuples, element_context),
        INT2OID => to_arrow_list_array!(pgrx::Array<i16>, tuples, element_context),
        INT4OID => to_arrow_list_array!(pgrx::Array<i32>, tuples, element_context),
        INT8OID => to_arrow_list_array!(pgrx::Array<i64>, tuples, element_context),
        UUIDOID => to_arrow_list_array!(pgrx::Array<Uuid>, tuples, element_context),
        NUMERICOID => {
            let precision = element_context.precision();

            if should_write_numeric_as_text(precision) {
                reset_fallback_to_text_context(element_typoid, element_typmod);

                to_arrow_list_array!(pgrx::Array<FallbackToText>, tuples, element_context)
            } else {
                to_arrow_list_array!(pgrx::Array<AnyNumeric>, tuples, element_context)
            }
        }
        BOOLOID => to_arrow_list_array!(pgrx::Array<bool>, tuples, element_context),
        DATEOID => to_arrow_list_array!(pgrx::Array<Date>, tuples, element_context),
        TIMEOID => to_arrow_list_array!(pgrx::Array<Time>, tuples, element_context),
        TIMETZOID => {
            to_arrow_list_array!(pgrx::Array<TimeWithTimeZone>, tuples, element_context)
        }
        TIMESTAMPOID => {
            to_arrow_list_array!(pgrx::Array<Timestamp>, tuples, element_context)
        }
        TIMESTAMPTZOID => {
            to_arrow_list_array!(pgrx::Array<TimestampWithTimeZone>, tuples, element_context)
        }
        CHAROID => to_arrow_list_array!(pgrx::Array<i8>, tuples, element_context),
        TEXTOID => to_arrow_list_array!(pgrx::Array<String>, tuples, element_context),
        JSONOID => to_arrow_list_array!(pgrx::Array<Json>, tuples, element_context),
        JSONBOID => to_arrow_list_array!(pgrx::Array<JsonB>, tuples, element_context),
        BYTEAOID => to_arrow_list_array!(pgrx::Array<&[u8]>, tuples, element_context),
        OIDOID => to_arrow_list_array!(pgrx::Array<Oid>, tuples, element_context),
        _ => {
            if element_context.is_composite() {
                let mut attribute_vals = vec![];

                let attribute_tupledesc = element_context.tupledesc();

                for tuple in tuples {
                    check_for_interrupts!();

                    if let Some(tuple) = tuple {
                        let attribute_val: Option<pgrx::Array<PgHeapTuple<AllocatedByRust>>> =
                            tuple
                                .get_by_index(
                                    NonZeroUsize::new(element_context.attnum() as usize)
                                        .expect("invalid attnum"),
                                )
                                .unwrap_or_else(|e| panic!("failed to get attribute: {}", e));

                        if let Some(attribute_val) = attribute_val {
                            let attribute_val = attribute_val
                                .iter()
                                .map(|tuple| tuple.map(|tuple| tuple.into_pg()))
                                .collect::<Vec<_>>();

                            let attribute_val = attribute_val
                                .iter()
                                .map(|tuple| {
                                    tuple.map(|tuple| unsafe {
                                        PgHeapTuple::from_heap_tuple(
                                            attribute_tupledesc.clone(),
                                            tuple,
                                        )
                                        .into_owned()
                                    })
                                })
                                .collect::<Vec<_>>();

                            attribute_vals.push(Some(attribute_val));
                        } else {
                            attribute_vals.push(None);
                        }
                    } else {
                        attribute_vals.push(None);
                    }
                }

                attribute_vals.to_arrow_array(element_context)
            } else if element_context.is_map() {
                reset_map_type_context(element_typoid);

                to_arrow_list_array!(pgrx::Array<Map>, tuples, element_context)
            } else if element_context.is_geometry() {
                to_arrow_list_array!(pgrx::Array<Geometry>, tuples, element_context)
            } else {
                reset_fallback_to_text_context(element_typoid, element_typmod);

                to_arrow_list_array!(pgrx::Array<FallbackToText>, tuples, element_context)
            }
        }
    }
}
