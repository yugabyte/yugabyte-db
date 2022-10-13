package models

// SlowQueryResponseYsqlQueryItem - Schema for Slow Query Response YSQL Query Item
type SlowQueryResponseYsqlQueryItem struct {

    Queryid int64 `json:"queryid"`

    Query string `json:"query"`

    Rolname string `json:"rolname"`

    Datname string `json:"datname"`

    Calls int32 `json:"calls"`

    LocalBlksHit int32 `json:"local_blks_hit"`

    LocalBlksWritten int32 `json:"local_blks_written"`

    MaxTime float32 `json:"max_time"`

    MeanTime float32 `json:"mean_time"`

    MinTime float32 `json:"min_time"`

    Rows int32 `json:"rows"`

    StddevTime float32 `json:"stddev_time"`

    TotalTime float32 `json:"total_time"`
}
