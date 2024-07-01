package models

// RecommendedRefactoringGraph - Refectoring recommendations for migrating SQL objects
type RecommendedRefactoringGraph struct {

    SqlType RefactoringCount `json:"sql_type"`

    Table RefactoringCount `json:"table"`

    View RefactoringCount `json:"view"`

    Function RefactoringCount `json:"function"`

    Triggers RefactoringCount `json:"triggers"`
}
