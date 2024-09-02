package models

// ErrorsAndSuggestionsDetails - Errors and suggestions details
type ErrorsAndSuggestionsDetails struct {

    IssueType string `json:"issueType"`

    ObjectType string `json:"objectType"`

    ObjectName string `json:"objectName"`

    Reason string `json:"reason"`

    SqlStatement string `json:"sqlStatement"`

    FilePath string `json:"filePath"`

    Suggestion string `json:"suggestion"`

    GH string `json:"GH"`
}
