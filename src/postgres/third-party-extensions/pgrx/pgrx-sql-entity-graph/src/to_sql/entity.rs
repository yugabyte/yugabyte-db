//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!

`sql = ...` fragment related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::SqlGraphEntity;
use crate::pgrx_sql::PgrxSql;
/// Represents configuration options for tuning the SQL generator.
///
/// When an item that can be rendered to SQL has these options at hand, they should be
/// respected. If an item does not have them, then it is not expected that the SQL generation
/// for those items can be modified.
///
/// The default configuration has `enabled` set to `true`, which indicates that the default SQL
/// generation behavior will be used.
///
/// When `enabled` is false, no SQL is generated for the item being configured.
///
#[derive(Default, Clone)]
pub struct ToSqlConfigEntity<'a> {
    pub enabled: bool,
    pub content: Option<&'a str>,
}
impl ToSqlConfigEntity<'_> {
    #[inline]
    fn fields(&self) -> (bool, Option<&str>) {
        (self.enabled, self.content)
    }
    /// Given a SqlGraphEntity, this function converts it to SQL based on the current configuration.
    ///
    /// If the config overrides the default behavior (i.e. using the `ToSql` trait), then `Some(eyre::Result)`
    /// is returned. If the config does not override the default behavior, then `None` is returned. This can
    /// be used to dispatch SQL generation in a single line, e.g.:
    ///
    /// ```rust,ignore
    /// config.to_sql(entity, context).unwrap_or_else(|| entity.to_sql(context))?
    /// ```
    pub fn to_sql(
        &self,
        entity: &SqlGraphEntity<'_>,
        context: &PgrxSql<'_>,
    ) -> Option<eyre::Result<String>> {
        if !self.enabled {
            return Some(Ok(format!(
                "\n\
                {sql_anchor_comment}\n\
                -- Skipped due to `#[pgrx(sql = false)]`\n",
                sql_anchor_comment = entity.sql_anchor_comment(),
            )));
        }

        if let Some(content) = self.content {
            let module_pathname = context.get_module_pathname();

            let content = content.replace("@MODULE_PATHNAME@", &module_pathname);

            return Some(Ok(format!(
                "\n\
                {sql_anchor_comment}\n\
                {content}\n\
            ",
                content = content,
                sql_anchor_comment = entity.sql_anchor_comment()
            )));
        }

        None
    }
}

impl std::cmp::PartialOrd for ToSqlConfigEntity<'_> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}
impl std::cmp::Ord for ToSqlConfigEntity<'_> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.fields().cmp(&other.fields())
    }
}
impl std::cmp::PartialEq for ToSqlConfigEntity<'_> {
    fn eq(&self, other: &Self) -> bool {
        self.fields() == other.fields()
    }
}
impl std::cmp::Eq for ToSqlConfigEntity<'_> {}
impl std::hash::Hash for ToSqlConfigEntity<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.fields().hash(state);
    }
}
impl std::fmt::Debug for ToSqlConfigEntity<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        let (enabled, content) = self.fields();
        f.debug_struct("ToSqlConfigEntity")
            .field("enabled", &enabled)
            .field("content", &content)
            .finish()
    }
}
