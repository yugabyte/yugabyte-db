use std::{collections::HashMap, fmt::Display, str::FromStr};

use arrow_schema::{DataType, Schema};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default)]
pub(crate) enum FieldIds {
    #[default]
    None,
    Auto,
    Explicit(FieldIdMapping),
}

/// Implements parsing for the field_ids option in COPY .. TO statements
impl FromStr for FieldIds {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "none" => Ok(FieldIds::None),
            "auto" => Ok(FieldIds::Auto),
            field_ids => Ok(FieldIds::Explicit(field_id_mapping_from_json_string(
                field_ids,
            )?)),
        }
    }
}

impl Display for FieldIds {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            FieldIds::None => write!(f, "none"),
            FieldIds::Auto => write!(f, "auto"),
            FieldIds::Explicit(field_id_mapping) => {
                write!(f, "{}", field_id_mapping_to_json_string(field_id_mapping))
            }
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(untagged)]
enum FieldIdMappingItem {
    FieldId(i32),
    FieldIdMapping(FieldIdMapping),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub(crate) struct FieldIdMapping {
    #[serde(flatten)]
    fields: HashMap<String, FieldIdMappingItem>,
}

impl FieldIdMapping {
    /// Returns the field ID, if any, from `FieldIdMapping` for the given field path.
    pub(crate) fn field_id(&self, field_path: &[String]) -> Option<i32> {
        if field_path.is_empty() {
            panic!("Field path is empty");
        }

        let field_name = &field_path[0];

        match self.fields.get(field_name) {
            Some(FieldIdMappingItem::FieldId(field_id)) => Some(*field_id),
            Some(FieldIdMappingItem::FieldIdMapping(field_id_mapping)) => {
                field_id_mapping.field_id(&field_path[1..])
            }
            None => None,
        }
    }

    /// Validates that every field name in the `FieldIdMapping` exists in the provided Arrow schema
    fn validate_against_schema(&self, arrow_schema: &Schema) -> Result<(), String> {
        // Build a map from field name to &Field for quick lookups
        let mut arrow_field_map = HashMap::new();
        for field in arrow_schema.fields() {
            arrow_field_map.insert(field.name().clone(), field);
        }

        // Check every field name in the JSON mapping
        for (field_name, mapping_item) in &self.fields {
            if field_name == "__root_field_id" {
                // Skip the root field, as it doesn't exist in the Arrow schema
                continue;
            }

            // Ensure the field exists in the Arrow schema
            let arrow_field = match arrow_field_map.get(field_name) {
                Some(f) => f,
                None => {
                    return Err(format!(
                    "Field '{}' in the mapping does not exist in the Arrow schema.\nAvailable fields: {:?}",
                    field_name,
                    arrow_schema
                        .fields()
                        .iter()
                        .map(|f| f.name())
                        .collect::<Vec<_>>()
                ));
                }
            };

            match mapping_item {
                // If the JSON item is an integer field ID, we're done
                FieldIdMappingItem::FieldId(_id) => {}

                // If the JSON item is a nested mapping, we need to validate it
                FieldIdMappingItem::FieldIdMapping(mapping) => match arrow_field.data_type() {
                    DataType::Struct(subfields) => {
                        // We expect the JSON keys to include something like:
                        //   "__root_field_id": <int>,
                        //   "field_name": <int or nested mapping>

                        let subschema = Schema::new(subfields.clone());
                        mapping.validate_against_schema(&subschema)?;
                    }
                    DataType::List(element_field) => {
                        // We expect the JSON keys to include something like:
                        //   "__root_field_id": <int>,
                        //   "element": <int or nested mapping>
                        //

                        let element_schema = Schema::new(vec![element_field.clone()]);
                        mapping.validate_against_schema(&element_schema)?;
                    }
                    DataType::Map(entry_field, _) => {
                        // We expect the JSON keys to include something like:
                        //   "__root_field_id": <int>,
                        //   "key": <int or nested mapping>
                        //   "val": <int or nested mapping>

                        match entry_field.data_type() {
                            DataType::Struct(entry_fields) => {
                                let entry_schema = Schema::new(entry_fields.clone());
                                mapping.validate_against_schema(&entry_schema)?;
                            }
                            other_type => {
                                panic!(
                                "Map entry field should be a struct, but got '{:?}' for field '{}'",
                                other_type, field_name
                            );
                            }
                        };

                        return Ok(());
                    }
                    other_type => {
                        panic!(
                            "Unexpected data type '{:?}' for field '{}'",
                            other_type, field_name
                        );
                    }
                },
            }
        }

        Ok(())
    }
}

pub(crate) fn field_id_mapping_from_json_string(
    json_string: &str,
) -> Result<FieldIdMapping, String> {
    serde_json::from_str(json_string).map_err(|_| "invalid JSON string for field_ids".into())
}

fn field_id_mapping_to_json_string(field_id_mapping: &FieldIdMapping) -> String {
    serde_json::to_string(field_id_mapping).unwrap()
}

/// Validate that every field name in the `FieldIdMapping` exists in the provided Arrow schema
/// when the `FieldIds` are explicitly specified.
pub(crate) fn validate_field_ids(field_ids: FieldIds, arrow_schema: &Schema) -> Result<(), String> {
    match field_ids {
        FieldIds::None => Ok(()),
        FieldIds::Auto => Ok(()),
        FieldIds::Explicit(field_id_mapping) => {
            field_id_mapping.validate_against_schema(arrow_schema)
        }
    }
}
