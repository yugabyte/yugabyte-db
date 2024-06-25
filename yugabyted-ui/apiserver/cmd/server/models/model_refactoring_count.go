package models

// RefactoringCount - count for automatic and manual refactoring
type RefactoringCount struct {

    Manual int32 `json:"manual"`

    Automatic int32 `json:"automatic"`
}
