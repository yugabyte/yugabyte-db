package models

// RecommendedRefactoringGraph - Refectoring recommendations for migrating SQL objects
type RecommendedRefactoringGraph struct {

    RefactorDetails []RefactoringCount `json:"refactor_details"`
}
