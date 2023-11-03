package config

import (
	_ "embed"
	"os"
	"path/filepath"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// yba-ctl.yml gets created via `make config`, and is not present by default
//
//go:embed yba-ctl.yml
var ReferenceYbaCtlConfig string

// WriteDefaultConfig writes the default config to /opt/yba-ctl/yba-ctl.yml (creating dirs if nec)
func WriteDefaultConfig() {
	cfgFile, err := os.Create(common.InputFile())
	if err != nil {
		log.Fatal("could not create input file: " + err.Error())
	}
	defer cfgFile.Close()

	_, err = cfgFile.WriteString(ReferenceYbaCtlConfig)
	if err != nil {
		log.Fatal("could not create input file: " + err.Error())
	}
	err = os.Chmod(common.InputFile(), 0644)
	if err != nil {
		log.Warn("failed to update config file permissions: " + err.Error())
	}
	if !common.HasSudoAccess() {
		// Update default installRoot to $HOME/yugabyte
		common.SetYamlValue(common.InputFile(), "installRoot",
			filepath.Join(common.GetUserHomeDir(), "yugabyte"))
	}
}
