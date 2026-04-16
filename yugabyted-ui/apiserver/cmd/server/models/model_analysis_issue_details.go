package models

// AnalysisIssueDetails - Details of issues in Schema Analysis Report
type AnalysisIssueDetails struct {

    IssueType string `json:"issueType"`

    ObjectName string `json:"objectName"`

    Reason string `json:"reason"`

    SqlStatement string `json:"sqlStatement"`

    FilePath string `json:"filePath"`

    Suggestion string `json:"suggestion"`

    GH string `json:"GH"`

    DocsLink string `json:"docs_link"`

    MinimumVersionsFixedIn []string `json:"minimum_versions_fixed_in"`
}
