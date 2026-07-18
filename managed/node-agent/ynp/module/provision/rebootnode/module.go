// Copyright (c) YugabyteDB, Inc.

package rebootnode

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "RebootNode"

type RebootNode struct {
	*config.BaseModule
}

func NewRebootNode(basePath string) config.Module {
	return &RebootNode{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "reboot_node")),
	}
}
