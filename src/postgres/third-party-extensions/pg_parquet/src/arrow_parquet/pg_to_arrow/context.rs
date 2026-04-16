use std::ops::Deref;

use arrow_schema::{FieldRef, Fields};
use pgrx::{
    pg_sys::{FormData_pg_attribute, Oid, NUMERICOID},
    PgTupleDesc,
};

use super::{
    array_element_typoid, collect_attributes_for, domain_array_base_elem_type,
    extract_precision_and_scale_from_numeric_typmod, is_array_type, is_composite_type, is_map_type,
    is_postgis_geometry_type, tuple_desc, CollectAttributesFor,
};

// PgToArrowAttributeContext contains the information needed to convert a PostgreSQL attribute
// to an Arrow array.
#[derive(Clone)]
pub(crate) struct PgToArrowAttributeContext {
    // common info for all types
    attnum: i16,
    typoid: Oid,
    typmod: i32,
    field: FieldRef,

    // type-specific info
    type_context: PgToArrowAttributeTypeContext,
}

impl Deref for PgToArrowAttributeContext {
    type Target = PgToArrowAttributeTypeContext;

    fn deref(&self) -> &Self::Target {
        &self.type_context
    }
}

impl PgToArrowAttributeContext {
    fn new(name: String, attnum: i16, typoid: Oid, typmod: i32, fields: Fields) -> Self {
        let field = fields
            .iter()
            .find(|field| field.name() == &name)
            .unwrap_or_else(|| panic!("failed to find field {}", name))
            .clone();

        let type_context =
            PgToArrowAttributeTypeContext::new(attnum, typoid, typmod, field.clone());

        Self {
            field,
            attnum,
            typoid,
            typmod,
            type_context,
        }
    }

    pub(crate) fn attnum(&self) -> i16 {
        self.attnum
    }

    pub(crate) fn typoid(&self) -> Oid {
        self.typoid
    }

    pub(crate) fn typmod(&self) -> i32 {
        self.typmod
    }

    pub(crate) fn field(&self) -> FieldRef {
        self.field.clone()
    }
}

// PgToArrowAttributeTypeContext contains type specific information needed to
// convert a PostgreSQL attribute to an Arrow array.
#[derive(Clone)]
pub(crate) enum PgToArrowAttributeTypeContext {
    Primitive {
        is_geometry: bool,
        precision: Option<u32>,
        scale: Option<u32>,
    },
    Array {
        element_context: Box<PgToArrowAttributeContext>,
    },
    Composite {
        tupledesc: PgTupleDesc<'static>,
        attribute_contexts: Vec<PgToArrowAttributeContext>,
    },
    Map {
        entries_context: Box<PgToArrowAttributeContext>,
    },
}

impl PgToArrowAttributeTypeContext {
    // constructors
    fn new(attnum: i16, typoid: Oid, typmod: i32, field: FieldRef) -> Self {
        if is_array_type(typoid) {
            Self::new_array(attnum, typoid, typmod, field)
        } else if is_composite_type(typoid) {
            Self::new_composite(typoid, typmod, field)
        } else if is_map_type(typoid) {
            Self::new_map(attnum, typoid, field)
        } else {
            Self::new_primitive(typoid, typmod)
        }
    }

    fn new_primitive(typoid: Oid, typmod: i32) -> Self {
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

        Self::Primitive {
            is_geometry,
            precision,
            scale,
        }
    }

    fn new_array(attnum: i16, typoid: Oid, typmod: i32, field: FieldRef) -> Self {
        let element_typoid = array_element_typoid(typoid);
        let element_typmod = typmod;

        let element_field = match field.data_type() {
            arrow::datatypes::DataType::List(field) => field.clone(),
            _ => unreachable!(),
        };

        let element_type_context = PgToArrowAttributeTypeContext::new(
            attnum,
            element_typoid,
            element_typmod,
            element_field.clone(),
        );

        let element_context = Box::new(PgToArrowAttributeContext {
            attnum,
            typoid: element_typoid,
            typmod: element_typmod,
            field: element_field,
            type_context: element_type_context,
        });

        Self::Array { element_context }
    }

    fn new_composite(typoid: Oid, typmod: i32, field: FieldRef) -> Self {
        let tupledesc = tuple_desc(typoid, typmod);
        let fields = match field.data_type() {
            arrow::datatypes::DataType::Struct(fields) => fields.clone(),
            _ => unreachable!(),
        };

        let attributes = collect_attributes_for(CollectAttributesFor::Other, &tupledesc);

        let attribute_contexts = collect_pg_to_arrow_attribute_contexts(&attributes, &fields);

        Self::Composite {
            tupledesc,
            attribute_contexts,
        }
    }

    fn new_map(attnum: i16, typoid: Oid, field: FieldRef) -> Self {
        let (entries_typoid, entries_typmod) = domain_array_base_elem_type(typoid);

        let entries_field = match field.data_type() {
            arrow::datatypes::DataType::Map(entries_field, _) => entries_field.clone(),
            _ => unreachable!(),
        };

        let entries_type_context = PgToArrowAttributeTypeContext::new(
            attnum,
            entries_typoid,
            entries_typmod,
            entries_field.clone(),
        );

        let entries_context = Box::new(PgToArrowAttributeContext {
            attnum,
            typoid: entries_typoid,
            typmod: entries_typmod,
            field: entries_field.clone(),
            type_context: entries_type_context,
        });

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

    pub(crate) fn attribute_contexts(&self) -> &Vec<PgToArrowAttributeContext> {
        match self {
            Self::Composite {
                attribute_contexts, ..
            } => attribute_contexts,
            _ => panic!("missing attribute contexts in context"),
        }
    }

    // map type methods
    pub(crate) fn entries_context(&self) -> &PgToArrowAttributeContext {
        match self {
            Self::Map { entries_context } => entries_context,
            _ => panic!("missing entries context in context"),
        }
    }

    // array type methods
    pub(crate) fn element_context(&self) -> &PgToArrowAttributeContext {
        match self {
            Self::Array {
                element_context, ..
            } => element_context,
            _ => panic!("not a context for an array type"),
        }
    }

    // type checks
    pub(crate) fn is_array(&self) -> bool {
        matches!(self, PgToArrowAttributeTypeContext::Array { .. })
    }

    pub(crate) fn is_composite(&self) -> bool {
        matches!(self, PgToArrowAttributeTypeContext::Composite { .. })
    }

    pub(crate) fn is_map(&self) -> bool {
        matches!(self, PgToArrowAttributeTypeContext::Map { .. })
    }

    pub(crate) fn is_geometry(&self) -> bool {
        match &self {
            PgToArrowAttributeTypeContext::Primitive { is_geometry, .. } => *is_geometry,
            _ => false,
        }
    }
}

pub(crate) fn collect_pg_to_arrow_attribute_contexts(
    attributes: &[FormData_pg_attribute],
    fields: &Fields,
) -> Vec<PgToArrowAttributeContext> {
    let mut attribute_contexts = vec![];

    for attribute in attributes {
        let attribute_name = attribute.name();
        let attribute_num = attribute.attnum;
        let attribute_typoid = attribute.type_oid().value();
        let attribute_typmod = attribute.type_mod();

        let attribute_context = PgToArrowAttributeContext::new(
            attribute_name.to_string(),
            attribute_num,
            attribute_typoid,
            attribute_typmod,
            fields.clone(),
        );

        attribute_contexts.push(attribute_context);
    }

    attribute_contexts
}
