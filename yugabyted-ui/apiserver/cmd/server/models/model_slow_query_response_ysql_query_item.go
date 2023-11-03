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

    MaxTime float64 `json:"max_time"`

    MeanTime float64 `json:"mean_time"`

    MinTime float64 `json:"min_time"`

    Rows int32 `json:"rows"`

    StddevTime float64 `json:"stddev_time"`

    TotalTime float64 `json:"total_time"`
}
