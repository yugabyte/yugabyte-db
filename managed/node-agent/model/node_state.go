// Copyright (c) YugaByte, Inc.

package model

type NodeState string

func (state NodeState) Name() string {
	return string(state)
}

const (
	Registering NodeState = "REGISTERING"
	Live        NodeState = "LIVE"
	Upgrade     NodeState = "UPGRADE"
	Upgrading   NodeState = "UPGRADING"
	Upgraded    NodeState = "UPGRADED"
)

type StateUpdateRequest struct {
	// Fill up version and state.
	CommonInfo
}

type VersionRequest struct {
	Version string `json:"version"`
}
