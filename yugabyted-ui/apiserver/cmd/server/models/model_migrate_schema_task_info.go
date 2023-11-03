package models

// MigrateSchemaTaskInfo - Voyager data migration metrics details
type MigrateSchemaTaskInfo struct {

    MigrationUuid string `json:"migration_uuid"`

    OverallStatus string `json:"overall_status"`

    ExportSchema string `json:"export_schema"`

    AnalyzeSchema string `json:"analyze_schema"`

    ImportSchema string `json:"import_schema"`

    SuggestionsErrors []ErrorsAndSuggestionsDetails `json:"suggestions_errors"`

    SqlObjects []SqlObjectsDetails `json:"sql_objects"`
}
