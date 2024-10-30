package models

// RefactoringCount - count for automatic and manual refactoring
type RefactoringCount struct {

    SqlObjectType string `json:"sql_object_type"`

    Manual int32 `json:"manual"`

    Automatic int32 `json:"automatic"`
}
