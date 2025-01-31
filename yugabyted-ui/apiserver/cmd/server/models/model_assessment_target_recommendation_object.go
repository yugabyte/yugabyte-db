package models

// AssessmentTargetRecommendationObject - Details about Target table recommendations
type AssessmentTargetRecommendationObject struct {

    NumOfColocatedTables int32 `json:"num_of_colocated_tables"`

    TotalSizeColocatedTables int64 `json:"total_size_colocated_tables"`

    NumOfShardedTable int32 `json:"num_of_sharded_table"`

    TotalSizeShardedTables int64 `json:"total_size_sharded_tables"`

    RecommendationDetails []TargetRecommendationItem `json:"recommendation_details"`
}
