use crate::log;
use pgrx::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// The list of supported Objects
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
enum SupportedObjects {
    Database,
    Role,
    Schema,
    Table,
    ForeignTable,
    View,
    MaterializedView,
    Column,
    Function,
}

impl std::fmt::Display for SupportedObjects {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Database => write!(f, "database"),
            Self::Role => write!(f, "role"),
            Self::Schema => write!(f, "schema"),
            Self::Table => write!(f, "table"),
            Self::ForeignTable => write!(f, "foreign table"),
            Self::View => write!(f, "view"),
            Self::MaterializedView => write!(f, "view"),
            Self::Column => write!(f, "column"),
            Self::Function => write!(f, "function"),
        }
    }
}

impl From<&str> for SupportedObjects {
    fn from(value: &str) -> Self {
        match value {
            "database" => Self::Database,
            "role" => Self::Role,
            "schema" => Self::Schema,
            "table" => Self::Table,
            "foreign table" => Self::ForeignTable,
            "view" => Self::View,
            "materialized view" => Self::MaterializedView,
            "column" => Self::Column,
            "function" => Self::Function,
            _ => panic!("Unsupported object type: {value}"),
        }
    }
}

/// Craft a seclabel from the provided information
fn to_seclabel(profile: &str, kind: SupportedObjects, name: &str, label: &str) -> String {
    let random = fastrand::u128(..).to_string();

    format!(
        "SECURITY LABEL FOR {} ON {} {} IS $label_{}$ {} $label_{}$;",
        profile,
        kind.to_string().to_uppercase(),
        name,
        random,
        label,
        random,
    )
}

/// A Record describing a database seclabel
#[derive(Debug, Clone)]
struct SecLabelInfo {
    objnamespace: String,
    objname: String,
    objtype: SupportedObjects,
    objsubname: Option<String>,
    objsubtype: Option<SupportedObjects>,
    full_objname: String,
    label: String,
}

impl<'conn> From<spi::SpiHeapTupleData<'conn>> for SecLabelInfo {
    fn from(value: spi::SpiHeapTupleData) -> Self {
        let objnamespace: String = value
            .get_by_name("objnamespace")
            .expect("Error while getting objnamespace from SpiTupleTable")
            .expect("Null value found for objnamespace in anon.seclabels");
        let objname: String = value
            .get_by_name("objname")
            .expect("Error while getting objname from SpiTupleTable")
            .expect("Null value found for objname in anon.seclabels");
        let objtype: SupportedObjects = SupportedObjects::from(
            value
                .get_by_name::<String, _>("objtype")
                .expect("Error while getting objtype from SpiTupleTable")
                .expect("Null value found for objtype in anon.seclabels")
                .as_ref(),
        );
        let objsubname: Option<String> = value
            .get_by_name("objsubname")
            .expect("Error while getting objsubname from SpiTupleTable");
        let objsubtype: Option<SupportedObjects> = value
            .get_by_name::<String, _>("objsubtype")
            .expect("Error while getting objsubtype from SpiTupleTable")
            .map(|v| SupportedObjects::from(v.as_ref()));
        let full_objname: String = value
            .get_by_name("full_objname")
            .expect("Error while getting full_objname from SpiTupleTable")
            .expect("Null value found for full_objname in anon.seclabels");
        let label: String = value
            .get_by_name("label")
            .expect("Error while getting label from SpiTupleTable")
            .expect("Null value found for label in anon.seclabels");

        Self {
            objnamespace,
            objname,
            objtype,
            objsubname,
            objsubtype,
            full_objname,
            label,
        }
    }
}

/// All the rules for a database
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
struct DatabaseRules {
    #[serde(skip_serializing_if = "Option::is_none")]
    mask: Option<String>,
    schemas: HashMap<String, SchemaRules>,
}

impl<'conn> From<spi::SpiHeapTupleData<'conn>> for DatabaseRules {
    fn from(value: spi::SpiHeapTupleData) -> Self {
        Self {
            mask: value
                .get_by_name::<String, _>("label")
                .expect("Error while getting label from SpiTupleTable"),
            schemas: HashMap::new(),
        }
    }
}

impl DatabaseRules {
    /// adds a seclabel to this database
    fn add(&mut self, sl: SecLabelInfo) -> Result<(), spi::Error> {
        fn add_object_to_schema(sc: &mut SchemaRules, sl: SecLabelInfo) {
            use SupportedObjects::*;
            match sl.objtype {
                Table | ForeignTable | View | MaterializedView | Column => {
                    sc.add_relation(
                        sl.objname,
                        sl.objtype,
                        sl.objsubname,
                        sl.objsubtype,
                        sl.label,
                    );
                }
                Function => {
                    sc.add_function(sl.full_objname, sl.label);
                }
                _ => {
                    unreachable!(
                        "Invalid object found in add_object_to_schema(): {}",
                        sl.objtype,
                    )
                }
            }
        }

        self.schemas
            .entry(sl.objnamespace.clone())
            .and_modify(|sc| {
                if let SupportedObjects::Schema = sl.objtype {
                    unreachable!("Schema {} already exists in HashMap", sl.objname);
                } else {
                    add_object_to_schema(sc, sl.clone());
                }
            })
            .or_insert_with(|| {
                if let SupportedObjects::Schema = sl.objtype {
                    SchemaRules::new(Some(sl.label))
                } else {
                    let mut sc = SchemaRules::new(None);
                    add_object_to_schema(&mut sc, sl);
                    sc
                }
            });

        Ok(())
    }

    /// Generate an array of the seclabel of all the items inside the database
    /// plus the seclabel of the database
    fn to_seclabels(&self, name: &str, profile: &str) -> Vec<String> {
        let mut seclabels = Vec::new();

        if let Some(mask) = &self.mask {
            seclabels.push(to_seclabel(profile, SupportedObjects::Database, name, mask));
        }

        // Since this a hashmap, we need an explicit sort to have a predicatable order.
        // it's mostly for testing purposes, but since this is not performance sensitive
        // we can do it anyway.
        let mut schemas: Vec<_> = self.schemas.iter().collect();
        schemas.sort_by_key(|(key, _)| *key);
        for (scname, scdata) in &schemas {
            seclabels.append(&mut scdata.to_seclabels(profile, scname));
        }

        seclabels
    }
}

/// Objects are stored in schemas, this structs store the seclabels for them
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct SchemaRules {
    #[serde(skip_serializing_if = "Option::is_none")]
    mask: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    functions: Option<HashMap<String, String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    tables: Option<HashMap<String, RelationRules>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    views: Option<HashMap<String, RelationRules>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    foreign_tables: Option<HashMap<String, RelationRules>>,
}

impl SchemaRules {
    /// Creates an empty schema
    fn new(rule: Option<String>) -> Self {
        Self {
            mask: rule,
            functions: None,
            tables: None,
            views: None,
            foreign_tables: None,
        }
    }

    /// add relations to the schema
    fn add_relation(
        &mut self,
        objname: String,
        objtype: SupportedObjects,
        objsubname: Option<String>,
        objsubtype: Option<SupportedObjects>,
        label: String,
    ) {
        match objsubtype {
            Some(SupportedObjects::Column) | None => (),
            _ => unreachable!(
                "Sub object {objsubname:?} of Relation {objname} is of type {objsubtype:?}"
            ),
        }

        match objtype {
            SupportedObjects::Table => self.tables.get_or_insert_with(HashMap::new),
            SupportedObjects::View => self.views.get_or_insert_with(HashMap::new),
            SupportedObjects::ForeignTable => self.foreign_tables.get_or_insert_with(HashMap::new),
            _ => unreachable!("Object {objsubname:?} is of type {objtype:?}: not a relation"),
        }
        .entry(objname.clone())
        .and_modify(|rel| {
            if let Some(subname) = objsubname.as_ref() {
                if rel.columns.insert(subname.clone(), label.clone()).is_some() {
                    unreachable!("Column {subname} already in HashMap of relation {objname}");
                }
            } else {
                unreachable!("Relation {objname} already exists in HashMap");
            }
        })
        .or_insert_with(|| {
            let mut rel =
                RelationRules::new(objsubname.as_ref().map_or(Some(label.clone()), |_| None));
            if let Some(subname) = objsubname {
                rel.add_column(subname, label);
            }
            rel
        });
    }

    /// add a function to the schema
    fn add_function(&mut self, objname: String, label: String) {
        let functions = self.functions.get_or_insert_with(HashMap::new);
        if functions.insert(objname.clone(), label).is_some() {
            unreachable!("Function {objname} already exists in HashMap");
        }
    }

    /// Generate an array of the seclabel of all the items inside the database
    /// plus the seclabel of the schema
    fn to_seclabels(&self, profile: &str, name: &str) -> Vec<String> {
        let mut seclabels = Vec::new();

        if let Some(mask) = &self.mask {
            seclabels.push(to_seclabel(profile, SupportedObjects::Schema, name, mask));
        }

        // Since we use hashmaps, we need an explicit sort them to have a predicatable order.
        // it's mostly for testing purposes, but since this is not performance sensitive
        // we can do it anyway.
        if let Some(tables) = self.tables.as_ref() {
            let mut tables: Vec<_> = tables.iter().collect();
            tables.sort_by_key(|(key, _)| *key);
            for (relname, reldata) in &tables {
                seclabels.extend(reldata.to_seclabels(
                    profile,
                    SupportedObjects::Table,
                    &format!("\"{}\".\"{}\"", name, relname)[..],
                ));
            }
        }

        if let Some(views) = self.views.as_ref() {
            let mut views: Vec<_> = views.iter().collect();
            views.sort_by_key(|(key, _)| *key);
            for (relname, reldata) in &views {
                seclabels.extend(reldata.to_seclabels(
                    profile,
                    SupportedObjects::View,
                    &format!("\"{}\".\"{}\"", name, relname)[..],
                ));
            }
        }

        if let Some(ft) = self.foreign_tables.as_ref() {
            let mut ft: Vec<_> = ft.iter().collect();
            ft.sort_by_key(|(key, _)| *key);
            for (relname, reldata) in &ft {
                seclabels.extend(reldata.to_seclabels(
                    profile,
                    SupportedObjects::ForeignTable,
                    &format!("\"{}\".\"{}\"", name, relname)[..],
                ));
            }
        }

        if let Some(functions) = self.functions.as_ref() {
            let mut functions: Vec<_> = functions.iter().collect();
            functions.sort_by_key(|(key, _)| *key);
            for (fnname, fndata) in &functions {
                seclabels.push(to_seclabel(
                    profile,
                    SupportedObjects::Function,
                    fnname.as_ref(),
                    fndata,
                ));
            }
        }

        seclabels
    }
}

/// A relations can be a table (classic, partitionned or foreign) or a view
/// (classic or materialized). They can contain columns which can be masked
/// The seclabels are stored in this structure
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RelationRules {
    #[serde(skip_serializing_if = "Option::is_none")]
    mask: Option<String>,
    columns: HashMap<String, String>,
}

impl RelationRules {
    /// Create an emptu relation
    fn new(rule: Option<String>) -> Self {
        Self {
            mask: rule,
            columns: HashMap::new(),
        }
    }

    fn add_column(&mut self, subname: String, label: String) {
        self.columns.insert(subname, label);
    }

    /// Generate the list of seclabels for the relation
    fn to_seclabels(&self, profile: &str, relkind: SupportedObjects, name: &str) -> Vec<String> {
        let mut seclabels = Vec::new();

        if let Some(mask) = &self.mask {
            seclabels.push(to_seclabel(profile, relkind, name, mask));
        }

        // Since this a hashmap, we need an explicit sort to have a predicatable order.
        // it's mostly for testing purposes, but since this is not performance sensitive
        // we can do it anyway.
        let mut columns: Vec<_> = self.columns.iter().collect();
        columns.sort_by_key(|(key, _)| *key);
        for (colname, coldata) in &columns {
            seclabels.push(to_seclabel(
                profile,
                SupportedObjects::Column,
                &format!("{}.\"{}\"", name, colname)[..],
                coldata,
            ));
        }

        seclabels
    }
}

/// Fetch all the roles rules for a provider.
///
/// This is separate from the current database rules since roles a instance
/// level items.
fn get_roles_rules_for_provider(
    client: &spi::SpiClient,
    provider: &str,
) -> Result<HashMap<String, String>, spi::Error> {
    Ok(client
        .select(
            "
                SELECT objname::text, label
                  FROM anon.seclabels
                 WHERE objtype = 'role'
                   AND provider = $1
            ",
            None,
            &[provider.into()],
        )?
        .map(|row| {
            (
                row.get_by_name::<String, _>("objname")
                    .expect("Error while getting role name from SpiTupleTable")
                    .expect("Null value found for role name in anon.seclabels"),
                row.get_by_name("label")
                    .expect("Error while getting label from SpiTupleTable")
                    .expect("Null value found for label in anon.seclabels"),
            )
        })
        .collect())
}

/// Fetch all the rules for the current database and provided provider and
/// returns aDatabaseRules object
fn get_rules_for_provider_in_current_database(
    client: &spi::SpiClient,
    provider: &str,
) -> Result<DatabaseRules, spi::Error> {
    let rows = client.select(
        "
            SELECT dbname::text , label
              FROM (VALUES (current_database())) AS F(dbname)
                    LEFT OUTER JOIN anon.seclabels ON objname = dbname AND objtype = 'database' AND provider = $1;
        ",
        None,
        &[provider.into()],
    )?;

    let mut database_rules = DatabaseRules::from(
        rows.into_iter()
            .next()
            // this should be unreachable because of the construction of the
            // query
            .expect("PostgreSQL coun't find the current database"),
    );

    let rows = client.select(
        "
             SELECT objnamespace::text, objname::text, objtype, objsubname, objsubtype, full_objname, label
               FROM anon.seclabels
              WHERE provider = $1
                AND objnamespace IS NOT NULL
                AND objtype IN ('schema', 'function', 'procedures', 'table', 'foreign table', 'view', 'materialized view')
              ORDER BY full_objname, objtype, objsubtype NULLS LAST
         ",
        None,
        &[provider.into()],
    )?.map(SecLabelInfo::from);

    for row in rows {
        database_rules.add(row)?;
    }

    Ok(database_rules)
}

/// Export all roles rules for a provider
///
/// The function declaration in PostgreSQL makes the provider default to "anon"
pub fn export_role_rules(provider: &str) -> pgrx::JsonB {
    Spi::connect(|client| {
        let roles = get_roles_rules_for_provider(client, provider)
            .expect("Error while fetching roles rules for a provider");

        pgrx::JsonB(serde_json::to_value(roles).expect("Serialization error"))
    })
}

/// Export all the current database rules for a provider
///
/// The function declaration in PostgreSQL makes the provider default to "anon"
pub fn export_current_database_rules(provider: &str) -> pgrx::JsonB {
    Spi::connect(|client| {
        let database = get_rules_for_provider_in_current_database(client, provider)
            .expect("Error while fetching the current database rules for provider");

        pgrx::JsonB(serde_json::to_value(database).expect("Serialization error"))
    })
}

/// Import a jsonb object that contains role rules for a provider
///
/// The function declaration in PostgreSQL makes the provider default to "anon"
pub fn import_role_rules(rules: pgrx::JsonB, provider: &str) {
    let roles: HashMap<String, String> =
        serde_json::from_value(rules.0).expect("Serialization error");

    for (role_name, role_data) in &roles {
        let query = to_seclabel(provider, SupportedObjects::Role, role_name, role_data);

        log::debug1!("{query:}");
        Spi::run(&query).expect("Failed to create seclabel");
    }
}

/// Import a jsonb object that contains rules for a provider in the current
/// database
///
/// The function declaration in PostgreSQL makes the provider default to "anon"
pub fn import_database_rules(rules: pgrx::JsonB, provider: &str) {
    let database: DatabaseRules = serde_json::from_value(rules.0).expect("Serialization error");
    let current_database: String = Spi::get_one("SELECT current_database()::text")
        .expect("Failed to retrieve current database name")
        .expect("No current database");

    for query in database.to_seclabels(current_database.as_ref(), provider) {
        log::debug1!("{query:}");
        Spi::run(&query).expect("Failed to create seclabel");
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::impexp::*;
    use std::collections::HashMap;

    #[test]
    /// Test the addition of objects to a database, goes thru schema, relation
    /// column and function additions
    fn test_database_add() {
        let mut db = DatabaseRules {
            mask: None,
            schemas: HashMap::new(),
        };

        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "public".into(),
            objtype: SupportedObjects::Schema,
            objsubname: None,
            objsubtype: None,
            full_objname: "public".into(),
            label: "TRUSTED".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "t".into(),
            objtype: SupportedObjects::Table,
            objsubname: None,
            objsubtype: None,
            full_objname: "public.t".into(),
            label: "TABLESAMPLE SYSTEM(44)".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "t".into(),
            objtype: SupportedObjects::Table,
            objsubname: Some("c".into()),
            objsubtype: Some(SupportedObjects::Column),
            full_objname: "public.t.c".into(),
            label: "MASKED WITH VALUE NULL".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "t2".into(),
            objtype: SupportedObjects::Table,
            objsubname: Some("c".into()),
            objsubtype: Some(SupportedObjects::Column),
            full_objname: "public.t2.c".into(),
            label: "MASKED WITH VALUE NULL".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "f".into(),
            objtype: SupportedObjects::Function,
            objsubname: None,
            objsubtype: None,
            full_objname: "public.f()".into(),
            label: "TRUSTED".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "v".into(),
            objtype: SupportedObjects::View,
            objsubname: Some("c".into()),
            objsubtype: Some(SupportedObjects::Column),
            full_objname: "public.v.c".into(),
            label: "MASKED WITH VALUE NULL".into(),
        })
        .unwrap();
        db.add(SecLabelInfo {
            objnamespace: "public".into(),
            objname: "ft".into(),
            objtype: SupportedObjects::ForeignTable,
            objsubname: Some("c".into()),
            objsubtype: Some(SupportedObjects::Column),
            full_objname: "public.ft.c".into(),
            label: "MASKED WITH VALUE NULL".into(),
        })
        .unwrap();

        let dbref = DatabaseRules {
            mask: None,
            schemas: HashMap::from([(
                "public".into(),
                SchemaRules {
                    mask: Some("TRUSTED".into()),
                    functions: Some(HashMap::from([("public.f()".into(), "TRUSTED".into())])),
                    tables: Some(HashMap::from([
                        (
                            "t".into(),
                            RelationRules {
                                mask: Some("TABLESAMPLE SYSTEM(44)".into()),
                                columns: HashMap::from([(
                                    "c".into(),
                                    "MASKED WITH VALUE NULL".into(),
                                )]),
                            },
                        ),
                        (
                            "t2".into(),
                            RelationRules {
                                mask: None,
                                columns: HashMap::from([(
                                    "c".into(),
                                    "MASKED WITH VALUE NULL".into(),
                                )]),
                            },
                        ),
                    ])),
                    views: Some(HashMap::from([(
                        "v".into(),
                        RelationRules {
                            mask: None,
                            columns: HashMap::from([("c".into(), "MASKED WITH VALUE NULL".into())]),
                        },
                    )])),
                    foreign_tables: Some(HashMap::from([(
                        "ft".into(),
                        RelationRules {
                            mask: None,
                            columns: HashMap::from([("c".into(), "MASKED WITH VALUE NULL".into())]),
                        },
                    )])),
                },
            )]),
        };

        assert_eq!(db, dbref);
    }

    #[test]
    #[should_panic(
        expected = "internal error: entered unreachable code: Relation t1 already exists in HashMap"
    )]
    fn test_panic_schema_duplicate_relation() {
        let mut sc = SchemaRules::new(Some("TRUSTED".into()));
        sc.add_relation(
            "t1".into(),
            SupportedObjects::Table,
            None,
            None,
            "TABLESAMPLE SYSTEM(33)".into(),
        );
        sc.add_relation(
            "t1".into(),
            SupportedObjects::Table,
            None,
            None,
            "TABLESAMPLE SYSTEM(33)".into(),
        );
    }

    #[test]
    #[should_panic(
        expected = "internal error: entered unreachable code: Function fc1 already exists in HashMap"
    )]
    fn test_panic_schema_duplicate_routine() {
        let mut sc = SchemaRules::new(Some("TRUSTED".into()));
        sc.add_function("fc1".into(), "TRUSTED".into());
        sc.add_function("fc1".into(), "TRUSTED".into());
    }

    #[test]
    #[should_panic(
        expected = "internal error: entered unreachable code: Column col1 already in HashMap"
    )]
    fn test_panic_relation_duplicate_column() {
        let mut sc = SchemaRules::new(Some("TRUSTED".into()));
        sc.add_relation(
            "t1".into(),
            SupportedObjects::Table,
            Some("col1".into()),
            Some(SupportedObjects::Column),
            "IS MASKED WITH VALUE NULL".into(),
        );
        sc.add_relation(
            "t1".into(),
            SupportedObjects::Table,
            Some("col1".into()),
            Some(SupportedObjects::Column),
            "IS MASKED WITH VALUE NULL".into(),
        );
    }

    #[test]
    #[should_panic(expected = "Unsupported object type: blah")]
    fn test_panic_unsupported_object() {
        let _ = SupportedObjects::from("blah");
    }
}
