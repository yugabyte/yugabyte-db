package models

// TargetClusterRecommendationDetails - Recommendations for the Target YugabyteDB cluster
type TargetClusterRecommendationDetails struct {

    RecommendationSummary string `json:"recommendation_summary"`

    TargetClusterRecommendation TargetClusterSpec `json:"target_cluster_recommendation"`

    TargetSchemaRecommendation TargetSchemaRecommendations `json:"target_schema_recommendation"`
}
