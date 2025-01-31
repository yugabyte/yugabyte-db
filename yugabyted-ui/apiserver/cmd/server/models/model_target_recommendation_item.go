package models

// TargetRecommendationItem - Target recommendation table metadata information
type TargetRecommendationItem struct {

    TableName string `json:"table_name"`

    DiskSize int64 `json:"disk_size"`

    SchemaRecommendation string `json:"schema_recommendation"`
}
