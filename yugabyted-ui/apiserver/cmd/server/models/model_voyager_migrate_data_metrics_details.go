package models

// VoyagerMigrateDataMetricsDetails - Voyager data migration metrics details
type VoyagerMigrateDataMetricsDetails struct {

    MigrationUuid string `json:"migration_uuid"`

    TableName string `json:"table_name"`

    SchemaName string `json:"schema_name"`

    MigrationPhase int32 `json:"migration_phase"`

    Status int32 `json:"status"`

    CountLiveRows int32 `json:"count_live_rows"`

    CountTotalRows int32 `json:"count_total_rows"`

    InvocationTimestamp string `json:"invocation_timestamp"`
}
