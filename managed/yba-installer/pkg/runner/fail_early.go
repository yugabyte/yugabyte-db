package runner

import (
	"errors"
	"os"
	"strings"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

const failEarlyFile string = "/opt/yba-ctl/.fail_step_name"

var failStepName string

func (r *Runner) failEarly(id string) bool {
	if os.Getenv("YBA_MODE") == "dev" && id == failStepName {
		log.Warn("dev yba mode enabled and step " + id + " is set to fail early from " + failEarlyFile)
		return true
	}
	return false
}

func loadFailEarlyFile() {
	data, err := os.ReadFile(failEarlyFile)
	if errors.Is(err, os.ErrNotExist) {
		log.DebugLF("fail early file does not exist, not loading")
	} else if err != nil {
		log.Warn("could not open fail early file: " + err.Error())
	} else {
		failStepName = strings.TrimSpace(string(data))
		log.Debug("found fail early step " + failStepName)
	}
}
func init() {
	loadFailEarlyFile()
}
