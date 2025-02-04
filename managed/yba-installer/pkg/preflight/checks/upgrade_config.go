package checks

import (
	"fmt"

	"github.com/spf13/viper"
)

var UpgradeConfigCheck = upgradeConfigCheck{
	"upgrade-config",
	true,
}

type upgradeConfigCheck struct {
	name        string
	skipAllowed bool
}

func (u upgradeConfigCheck) Name() string {
	return u.name
}

func (u upgradeConfigCheck) SkipAllowed() bool {
	return u.skipAllowed
}

func (u upgradeConfigCheck) Execute() Result {
	res := Result{
		Check:  u.name,
		Status: StatusPassed,
	}

	// Check that certs are specified
	if viper.GetString("server_cert_path") == "" || viper.GetString("server_key_path") == "" {
		res.Status = StatusCritical
		res.Error = fmt.Errorf("server_cert_path and server_key_path must be set")
	}

	return res
}
