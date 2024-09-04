package models

// UnsupportedSqlWithDetails - Schema for Suggested refactoring tab in Migrate Schema page
type UnsupportedSqlWithDetails struct {

    SuggestionsErrors []ErrorsAndSuggestionsDetails `json:"suggestions_errors"`

    UnsupportedType string `json:"unsupported_type"`

    Count int32 `json:"count"`
}
