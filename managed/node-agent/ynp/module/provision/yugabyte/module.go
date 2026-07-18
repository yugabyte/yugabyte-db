// Copyright (c) YugabyteDB, Inc.

package yugabyte

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "CreateYugabyteUser"

type CreateYugabyteUser struct {
	*config.BaseModule
}

func NewCreateYugabyteUser(basePath string) config.Module {
	return &CreateYugabyteUser{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "yugabyte")),
	}
}
