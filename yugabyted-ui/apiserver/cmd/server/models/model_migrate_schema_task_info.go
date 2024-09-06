package models

// MigrateSchemaTaskInfo - Voyager data migration metrics details
type MigrateSchemaTaskInfo struct {

    MigrationUuid string `json:"migration_uuid"`

    OverallStatus string `json:"overall_status"`

    ExportSchema string `json:"export_schema"`

    AnalyzeSchema string `json:"analyze_schema"`

    ImportSchema string `json:"import_schema"`

    SuggestionsErrors []ErrorsAndSuggestionsDetails `json:"suggestions_errors"`

    CurrentAnalysisReport SchemaAnalysisReport `json:"current_analysis_report"`

    AnalysisHistory []SchemaAnalysisReport `json:"analysis_history"`

    SqlObjects []SqlObjectsDetails `json:"sql_objects"`
}
