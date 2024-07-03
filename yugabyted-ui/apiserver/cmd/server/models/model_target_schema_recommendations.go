package models

// TargetSchemaRecommendations - Target YugabyteDB cluster schema recommendations
type TargetSchemaRecommendations struct {

    NoOfColocatedTables int64 `json:"no_of_colocated_tables"`

    TotalSizeColocatedTables int64 `json:"total_size_colocated_tables"`

    NoOfShardedTables int64 `json:"no_of_sharded_tables"`

    TotalSizeShardedTables int64 `json:"total_size_sharded_tables"`
}
