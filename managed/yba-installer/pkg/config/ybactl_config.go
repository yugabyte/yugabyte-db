package config

import (
	_ "embed"
)

// yba-ctl.yml gets created via `make config`, and is not present by default
//
//go:embed yba-ctl.yml
var ReferenceYbaCtlConfig string
