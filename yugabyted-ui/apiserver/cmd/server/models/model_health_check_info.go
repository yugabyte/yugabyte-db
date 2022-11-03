package models

type HealthCheckInfo struct {

    // UUIDs of dead nodes
    DeadNodes []string `json:"dead_nodes"`

    MostRecentUptime int64 `json:"most_recent_uptime"`

    // UUIDs of under-replicated tablets
    UnderReplicatedTablets []string `json:"under_replicated_tablets"`

    // UUIDs of leaderless tablets
    LeaderlessTablets []string `json:"leaderless_tablets"`
}
