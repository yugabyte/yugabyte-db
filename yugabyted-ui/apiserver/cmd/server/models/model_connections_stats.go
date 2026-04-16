package models

// ConnectionsStats - YSQL connection manager stats
type ConnectionsStats struct {

    Data map[string][]ConnectionStatsItem `json:"data"`
}
