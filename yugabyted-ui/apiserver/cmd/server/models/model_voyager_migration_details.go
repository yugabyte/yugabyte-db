package models

// VoyagerMigrationDetails - Information regarding a specific voyager migration task
type VoyagerMigrationDetails struct {

    MigrationUuid string `json:"migration_uuid"`

    MigrationName string `json:"migration_name"`

    MigrationPhase int32 `json:"migration_phase"`

    InvocationSequence int32 `json:"invocation_sequence"`

    SourceDb string `json:"source_db"`

    Complexity string `json:"complexity"`

    DatabaseName string `json:"database_name"`

    SchemaName string `json:"schema_name"`

    Status string `json:"status"`

    InvocationTimestamp string `json:"invocation_timestamp"`
}
