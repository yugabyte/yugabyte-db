package models

// VoyagerMigrationDetails - Information regarding a specific voyager migration task
type VoyagerMigrationDetails struct {

    MigrationUuid string `json:"migration_uuid"`

    MigrationName string `json:"migration_name"`

    MigrationType string `json:"migration_type"`

    MigrationPhase int32 `json:"migration_phase"`

    InvocationSequence int32 `json:"invocation_sequence"`

    SourceDb VoyagerMigrationDetailsSourceDb `json:"source_db"`

    Voyager VoyagerMigrationDetailsVoyager `json:"voyager"`

    TargetCluster VoyagerMigrationDetailsTargetCluster `json:"target_cluster"`

    Complexity string `json:"complexity"`

    Status string `json:"status"`

    Progress string `json:"progress"`

    InvocationTimestamp string `json:"invocation_timestamp"`

    StartTimestamp string `json:"start_timestamp"`
}
