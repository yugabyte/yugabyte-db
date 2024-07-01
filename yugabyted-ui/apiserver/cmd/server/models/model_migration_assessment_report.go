package models

// MigrationAssessmentReport - Details of the migration assessment report
type MigrationAssessmentReport struct {

    Summary AssessmentReportSummary `json:"summary"`

    SourceEnvironment SourceEnvironmentInfo `json:"source_environment"`

    SourceDatabase SourceDatabaseInfo `json:"source_database"`

    TargetRecommendations TargetClusterRecommendationDetails `json:"target_recommendations"`

    RecommendedRefactoring RecommendedRefactoringGraph `json:"recommended_refactoring"`

    UnsupportedDataTypes []UnsupportedSqlInfo `json:"unsupported_data_types"`

    UnsupportedFunctions []UnsupportedSqlInfo `json:"unsupported_functions"`

    UnsupportedFeatures []UnsupportedSqlInfo `json:"unsupported_features"`
}
