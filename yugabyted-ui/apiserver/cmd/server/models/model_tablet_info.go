package models

// TabletInfo - Tablet Info
type TabletInfo struct {

    TabletId string `json:"tablet_id"`

    Partition string `json:"partition"`

    SplitDepth int32 `json:"split_depth"`

    State string `json:"state"`

    Hidden bool `json:"hidden"`

    Message string `json:"message"`

    RaftConfig []RaftConfig `json:"raft_config"`
}
