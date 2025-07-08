use std::ops::Deref;

use arrow_schema::{DataType, FieldRef, Fields};
use pgrx::{
    pg_sys::{FormData_pg_attribute, Oid, NUMERICOID},
    PgTupleDesc,
};

use crate::type_compat::pg_arrow_type_conversions::extract_precision_and_scale_from_numeric_typmod;

use super::{
    array_element_typoid, collect_attributes_for, domain_array_base_elem_type, is_array_type,
    is_composite_type, is_map_type, is_postgis_geometry_type, tuple_desc, CollectAttributesFor,
};

// ArrowToPgAttributeContext contains the information needed to convert an Arrow array
// to a PostgreSQL attribute.
#[derive(Clone)]
pub(crate) struct ArrowToPgAttributeContext {
    // common info for all types
    name: String,
    data_type: DataType,
    needs_cast: bool,
    typoid: Oid,
    typmod: i32,

    // type-specific info
    type_context: ArrowToPgAttributeTypeContext,
}

impl Deref for ArrowToPgAttributeContext {
    type Target = ArrowToPgAttributeTypeContext;

    fn deref(&self) -> &Self::Target {
        &self.type_context
    }
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

        let type_context = ArrowToPgAttributeTypeContext::new(typoid, typmod, &data_type);

        Self {
            name: name.to_string(),
            data_type,
            needs_cast,
            typoid,
            typmod,
            type_context,
        }
    }

    pub(crate) fn typoid(&self) -> Oid {
        self.typoid
    }

    pub(crate) fn typmod(&self) -> i32 {
        self.typmod
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

    pub(crate) fn timezone(&self) -> &str {
        let timezone = match &self.type_context {
            ArrowToPgAttributeTypeContext::Primitive { timezone, .. } => timezone.as_ref(),
            _ => None,
        };

        timezone.unwrap_or_else(|| panic!("missing timezone in context"))
    }
}

// ArrowToPgAttributeTypeContext contains type specific information needed to
// convert an Arrow array to a PostgreSQL attribute.
#[derive(Clone)]
pub(crate) enum ArrowToPgAttributeTypeContext {
    Primitive {
        is_geometry: bool,
        precision: Option<u32>,
        scale: Option<u32>,
        timezone: Option<String>,
    },
    Array {
        element_context: Box<ArrowToPgAttributeContext>,
    },
    Composite {
        tupledesc: PgTupleDesc<'static>,
        attribute_contexts: Vec<ArrowToPgAttributeContext>,
    },
    Map {
        entries_context: Box<ArrowToPgAttributeContext>,
    },
}

impl ArrowToPgAttributeTypeContext {
    // constructors
    fn new(typoid: Oid, typmod: i32, data_type: &DataType) -> Self {
        if is_array_type(typoid) {
            Self::new_array(typoid, typmod, data_type)
        } else if is_composite_type(typoid) {
            Self::new_composite(typoid, typmod, data_type)
        } else if is_map_type(typoid) {
            Self::new_map(typoid, data_type)
        } else {
            Self::new_primitive(typoid, typmod, data_type)
        }
    }

    fn new_primitive(typoid: Oid, typmod: i32, data_type: &DataType) -> Self {
        let precision;
        let scale;
        if typoid == NUMERICOID {
            let (p, s) = extract_precision_and_scale_from_numeric_typmod(typmod);
            precision = Some(p);
            scale = Some(s);
        } else {
            precision = None;
            scale = None;
        }

        let is_geometry = is_postgis_geometry_type(typoid);

        let timezone = match &data_type {
            DataType::Timestamp(_, Some(timezone)) => Some(timezone.to_string()),
            _ => None,
        };

        Self::Primitive {
            is_geometry,
            precision,
            scale,
            timezone,
        }
    }

    fn new_array(typoid: Oid, typmod: i32, data_type: &DataType) -> Self {
        let element_typoid = array_element_typoid(typoid);
        let element_typmod = typmod;

        let element_field = match data_type {
            DataType::List(field) => field.clone(),
            _ => unreachable!(),
        };

        let element_context = Box::new(ArrowToPgAttributeContext::new(
            element_field.name(),
            element_typoid,
            element_typmod,
            element_field.clone(),
            None,
        ));

        Self::Array { element_context }
    }

    fn new_composite(typoid: Oid, typmod: i32, data_type: &DataType) -> Self {
        let tupledesc = tuple_desc(typoid, typmod);
        let fields = match data_type {
            arrow::datatypes::DataType::Struct(fields) => fields.clone(),
            _ => unreachable!(),
        };

        let attributes = collect_attributes_for(CollectAttributesFor::Other, &tupledesc);

        // we only cast the top-level attributes, which already covers the nested attributes
        let cast_to_types = None;

        let attribute_contexts =
            collect_arrow_to_pg_attribute_contexts(&attributes, &fields, cast_to_types);

        Self::Composite {
            tupledesc,
            attribute_contexts,
        }
    }

    fn new_map(typoid: Oid, data_type: &DataType) -> Self {
        let (entries_typoid, entries_typmod) = domain_array_base_elem_type(typoid);

        let entries_field = match data_type {
            arrow::datatypes::DataType::Map(entries_field, _) => entries_field.clone(),
            _ => unreachable!(),
        };

        let entries_context = Box::new(ArrowToPgAttributeContext::new(
            entries_field.name(),
            entries_typoid,
            entries_typmod,
            entries_field.clone(),
            None,
        ));

        Self::Map { entries_context }
    }

    // primitive type methods
    pub(crate) fn precision(&self) -> u32 {
        let precision = match self {
            Self::Primitive { precision, .. } => *precision,
            _ => None,
        };

        precision.unwrap_or_else(|| panic!("missing precision in context"))
    }

    pub(crate) fn scale(&self) -> u32 {
        let scale = match self {
            Self::Primitive { scale, .. } => *scale,
            _ => None,
        };

        scale.unwrap_or_else(|| panic!("missing scale in context"))
    }

    // composite type methods
    pub(crate) fn tupledesc(&self) -> PgTupleDesc<'static> {
        match self {
            Self::Composite { tupledesc, .. } => tupledesc.clone(),
            _ => panic!("missing tupledesc in context"),
        }
    }

    pub(crate) fn attribute_contexts(&self) -> &Vec<ArrowToPgAttributeContext> {
        match self {
            Self::Composite {
                attribute_contexts, ..
            } => attribute_contexts,
            _ => panic!("missing attribute contexts in context"),
        }
    }

    // map type methods
    pub(crate) fn entries_context(&self) -> &ArrowToPgAttributeContext {
        match self {
            Self::Map { entries_context } => entries_context,
            _ => panic!("missing entries context in context"),
        }
    }

    // array type methods
    pub(crate) fn element_context(&self) -> &ArrowToPgAttributeContext {
        match self {
            Self::Array {
                element_context, ..
            } => element_context,
            _ => panic!("not a context for an array type"),
        }
    }

    // type checks
    pub(crate) fn is_geometry(&self) -> bool {
        match &self {
            ArrowToPgAttributeTypeContext::Primitive { is_geometry, .. } => *is_geometry,
            _ => false,
        }
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
