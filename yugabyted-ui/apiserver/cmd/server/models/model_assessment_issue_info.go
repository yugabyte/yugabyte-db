package models

// AssessmentIssueInfo -
// Assessment Issues of a particular issue type (migration caveat, datatype, etc)
type AssessmentIssueInfo struct {

    Type string `json:"type"`

    Name string `json:"name"`

    Description string `json:"description"`

    Count int32 `json:"count"`

    Impact string `json:"impact"`

    Objects []UnsupportedSqlObjectData `json:"objects"`

    DocsLink string `json:"docs_link"`

    MinimumVersionsFixedIn []string `json:"minimum_versions_fixed_in"`
}
