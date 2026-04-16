package models

// VoyagerDataMigrationMetricsInfo - Voyager data migration metrics details
type VoyagerDataMigrationMetricsInfo struct {

    MigrationUuid string `json:"migration_uuid"`

    TableName string `json:"table_name"`

    SchemaName string `json:"schema_name"`

    MigrationPhase int32 `json:"migration_phase"`

    Complexity string `json:"complexity"`

    Status int32 `json:"status"`

    CountLiveRows int32 `json:"count_live_rows"`

    CountTotalRows int32 `json:"count_total_rows"`

    InvocationTimestamp string `json:"invocation_timestamp"`
}
