package models

// RefactoringCount -
// Count of automatic, manual and invalid SQL objects displayed in Schema analysis page
type RefactoringCount struct {

    SqlObjectType string `json:"sql_object_type"`

    Manual int32 `json:"manual"`

    Automatic int32 `json:"automatic"`

    Invalid int32 `json:"invalid"`
}
