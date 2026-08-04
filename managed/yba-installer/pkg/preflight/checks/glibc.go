package checks

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

const minSupportedGlibcVersion = "2.28"

var Glibc = &glibcCheck{
	name:        "glibc",
	skipAllowed: false,
}

type glibcCheck struct {
	name        string
	skipAllowed bool
}

func (g glibcCheck) Name() string {
	return g.name
}
func (g glibcCheck) SkipAllowed() bool {
	return g.skipAllowed
}
func (g glibcCheck) Execute() Result {
	res := Result{
		Check:  g.name,
		Status: StatusPassed,
	}
	// Check glibc version
	version, err := getGlibcVersion()
	if err != nil {
		res.Status = StatusCritical
		res.Error = err
		return res
	}
	if !isGlibcVersionSupported(version) {
		res.Status = StatusCritical
		res.Error = fmt.Errorf("glibc version %s is not supported. Required %s", version, minSupportedGlibcVersion)
		return res
	}

	return res
}

func isGlibcVersionSupported(version string) bool {
	aParts := strings.Split(version, ".")                  // Actual version
	eParts := strings.Split(minSupportedGlibcVersion, ".") // Expected version
	for i := 0; i < len(eParts); i++ {
		if i >= len(aParts) {
			// If actual version has more parts and all previous parts matched, it's still supported
			break
		}
		aPart, err := strconv.Atoi(aParts[i])
		if err != nil {
			logging.Error(fmt.Sprintf("failed to parse glibc version part: %s", aParts[i]))
			return false
		}
		ePart, err := strconv.Atoi(eParts[i])
		if err != nil {
			logging.Error(fmt.Sprintf("failed to parse expected glibc version part: %s", eParts[i]))
			return false
		}
		if aPart < ePart {
			logging.Error(fmt.Sprintf("glibc version %s is lower than required %s", version, minSupportedGlibcVersion))
			return false
		}
	}

	return true
}

func getGlibcVersion() (string, error) {
	output := shell.Run("ldd", "--version")
	if !output.SucceededOrLog() {
		return "", fmt.Errorf("failed to run ldd command: %w", output.Error)
	}

	// The first line of the output contains the version information
	lines := output.StdoutString()
	if len(lines) == 0 {
		return "", fmt.Errorf("no output from ldd command")
	}

	// Matches ldd (*) <version>
	re := regexp.MustCompile(`ldd\s+\([^)]+\)\s+(\d+\.\d+)`)
	matches := re.FindStringSubmatch(lines)
	if len(matches) < 2 {
		return "", fmt.Errorf("failed to parse glibc version from output: %s", lines)
	}

	return matches[1], nil
}
