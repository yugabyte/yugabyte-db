// Copyright (c) YugabyteDB, Inc.

package disablednfautomatic

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "DisabledDnfAutomatic"

// DisabledDnfAutomatic represents the disabled dnf automatic configuration module.
type DisabledDnfAutomatic struct {
	*config.BaseModule
}

func NewDisabledDnfAutomatic(basePath string) config.Module {
	return &DisabledDnfAutomatic{
		BaseModule: config.NewBaseModule(
			ModuleName,
			filepath.Join(basePath, "disabled_dnf_automatic"),
		),
	}
}
