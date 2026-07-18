package models

// AssessmentCategoryInfo -
// Assessment Issues aggrageted based on Issue Category (migration caveat,datatype,etc)
type AssessmentCategoryInfo struct {

    Category string `json:"category"`

    CategoryDescription string `json:"category_description"`

    Issues []AssessmentIssueInfo `json:"issues"`
}
