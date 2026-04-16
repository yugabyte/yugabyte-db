package errorhandler

import (
	"errors"
	"fmt"
	"os"
	"runtime"
	"strconv"

	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// FailEarlyError is raised when yba-installer decides to fail even if a step is successful. this is
// only to help with impotency testing.
var FailEarlyError = errors.New("yba-installer is failing early")

var counter int = 0

func Handle(err error, loggingContext string) error {
	counter++
	if err == nil && failEarly() {
		_, filename, lineNo, _ := runtime.Caller(1)
		return fmt.Errorf("handle fail early %s:%d: %w", filename, lineNo, FailEarlyError)
	}
	return err
}

func TotalSteps() int {
	return counter
}

const failEarlyFile string = "/opt/yba-ctl/.fail_step_name"

var failStepCount int

func failEarly() bool {
	if os.Getenv("YBA_MODE") == "dev" && counter == failStepCount {
		log.Warn(
			fmt.Sprintf("dev yba mode enabled and step %d is set to fail early from %s ",
				counter, failEarlyFile))
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
		failStepCount, _ = strconv.Atoi(string(data))
		log.Debug(fmt.Sprintf("found fail early step %d", failStepCount))
	}
}
func init() {
	loadFailEarlyFile()
}
