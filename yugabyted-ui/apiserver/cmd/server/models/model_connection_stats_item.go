package models

type ConnectionStatsItem struct {

    // GH #19722 : Structure of Ysql Connection Manager stats have changed.
    Pool string `json:"pool"`

    ActiveLogicalConnections int64 `json:"active_logical_connections"`

    QueuedLogicalConnections int64 `json:"queued_logical_connections"`

    IdleOrPendingLogicalConnections int64 `json:"idle_or_pending_logical_connections"`

    ActivePhysicalConnections int64 `json:"active_physical_connections"`

    IdlePhysicalConnections int64 `json:"idle_physical_connections"`

    AvgWaitTimeNs int64 `json:"avg_wait_time_ns"`

    Qps int64 `json:"qps"`

    Tps int64 `json:"tps"`
}
