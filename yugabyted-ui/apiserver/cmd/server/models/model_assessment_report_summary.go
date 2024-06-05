package models

// AssessmentReportSummary - API for getting migration assessment summary
type AssessmentReportSummary struct {

    MigrationComplexity string `json:"migration_complexity"`

    EstimatedMigrationTime string `json:"estimated_migration_time"`

    Summary string `json:"summary"`
}
