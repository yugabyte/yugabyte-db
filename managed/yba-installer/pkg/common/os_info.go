package common

import (
	"fmt"
	"log"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"gopkg.in/ini.v1"
)

// OSName returns the name of the operating system.
func OSName() string {
	cfg, err := ini.Load("/etc/os-release")
	if err != nil {
		log.Fatal(err)
	}
	return fmt.Sprintf("%s-%s",
		cfg.Section("").Key("NAME").String(),
		cfg.Section("").Key("VERSION").String(),
	)
}

// PythonVersion gets the python3 version
func PythonVersion() string {
	out := shell.Run("python3", "--version")
	if !out.SucceededOrLog() {
		log.Fatal(out.Error.Error())
	}
	return out.StdoutString()
}

// MemoryHuman returns the memory in human readable form (GB)
func MemoryHuman() string {
	command := "grep"
	args := []string{"MemTotal", "/proc/meminfo"}
	out := shell.Run(command, args...)
	if !out.SucceededOrLog() {
		log.Fatal(out.Error)
	}
	field1 := strings.Fields(out.StdoutString())[1]
	availableMemoryKB, _ := strconv.Atoi(strings.Split(field1, " ")[0])
	availableMemoryGB := float64(availableMemoryKB) / 1e6
	return fmt.Sprintf("%vGB", availableMemoryGB)
}
