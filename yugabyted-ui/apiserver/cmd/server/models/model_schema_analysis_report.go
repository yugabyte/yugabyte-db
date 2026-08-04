package models

// SchemaAnalysisReport - Voyager data migration metrics details
type SchemaAnalysisReport struct {

    VoyagerVersion string `json:"voyager_version"`

    TargetDbVersion string `json:"target_db_version"`

    SqlObjects []SqlObjectsDetails `json:"sql_objects"`

    RecommendedRefactoring []RefactoringCount `json:"recommended_refactoring"`
}
