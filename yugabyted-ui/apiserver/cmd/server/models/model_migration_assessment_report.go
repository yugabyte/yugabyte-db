package models

// MigrationAssessmentReport - Details of the migration assessment report
type MigrationAssessmentReport struct {

    OperatingSystem string `json:"operating_system"`

    VoyagerVersion string `json:"voyager_version"`

    TargetDbVersion string `json:"target_db_version"`

    AssessmentStatus bool `json:"assessment_status"`

    Summary AssessmentReportSummary `json:"summary"`

    SourceEnvironment SourceEnvironmentInfo `json:"source_environment"`

    SourceDatabase SourceDatabaseInfo `json:"source_database"`

    TargetRecommendations TargetClusterRecommendationDetails `json:"target_recommendations"`

    RecommendedRefactoring []RefactoringCount `json:"recommended_refactoring"`

    AssessmentIssues []AssessmentCategoryInfo `json:"assessment_issues"`
}
