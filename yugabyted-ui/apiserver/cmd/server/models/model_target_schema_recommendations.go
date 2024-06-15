package models

// TargetSchemaRecommendations - Target YugabyteDB cluster schema recommendations
type TargetSchemaRecommendations struct {

    NoOfColocatedTables int32 `json:"no_of_colocated_tables"`

    TotalSizeColocatedTables int32 `json:"total_size_colocated_tables"`

    NoOfShardedTables int32 `json:"no_of_sharded_tables"`

    TotalSizeShardedTables int32 `json:"total_size_sharded_tables"`
}
