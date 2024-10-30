package models

// SchemaAnalysisReport - Voyager data migration metrics details
type SchemaAnalysisReport struct {

    UnsupportedFunctions []UnsupportedSqlWithDetails `json:"unsupported_functions"`

    UnsupportedFeatures []UnsupportedSqlWithDetails `json:"unsupported_features"`

    UnsupportedDatatypes []UnsupportedSqlWithDetails `json:"unsupported_datatypes"`

    RecommendedRefactoring RecommendedRefactoringGraph `json:"recommended_refactoring"`
}
