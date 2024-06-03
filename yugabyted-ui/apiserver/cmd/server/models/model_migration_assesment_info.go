package models

// MigrationAssesmentInfo - Details for Voyager migrations assesment
type MigrationAssesmentInfo struct {

    AssesmentStatus bool `json:"assesment_status"`

    TopErrors []string `json:"top_errors"`

    TopSuggestions []string `json:"top_suggestions"`

    ComplexityOverview []AssesmentComplexityInfo `json:"complexity_overview"`
}
