UPDATE schema_version
SET type = 'JDBC', checksum = NULL, description = 'Create New Alert Definitions Extra Migration', script = 'db.migration.default.common.V68__Create_New_Alert_Definitions_Extra_Migration'
WHERE version = '68' AND checksum = -1455400612;