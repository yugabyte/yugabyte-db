package systemd

import (
	"fmt"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
)

func LingerEnable() error {
	return runLoginctlCmd("enable-linger", viper.GetString("service_username"))
}

func LingerDisable() error {
	return runLoginctlCmd("disable-linger", viper.GetString("service_username"))
}

func runLoginctlCmd(cmd string, args ...string) error {
	cmdArgs := append([]string{cmd}, args...)
	out := shell.Run("loginctl", cmdArgs...)
	if !out.SucceededOrLog() {
		return fmt.Errorf("loginctl %s failed: %w", cmd, out.Error)
	}
	return nil

}
