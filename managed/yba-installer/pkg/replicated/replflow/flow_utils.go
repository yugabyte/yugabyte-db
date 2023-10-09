package replflow

import (
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

// stopContainer runs docker stop on the container
func stopContainer(name string) error {
	if out := shell.Run("docker", "stop", name); !out.Succeeded() {
		if strings.Contains(out.StderrString(), "No such container") {
			logging.Debug("skipping stop of container " + name + " - doesn't exist")
			return nil
		}
		out.SucceededOrLog()
		return fmt.Errorf("could not stop container %s: %w", name, out.Error)
	}
	return nil
}

// forceDeleteContainers removes containers with 'docker rm -f'
func forceDeleteContainer(containers ...string) error {
	args := append([]string{"rm", "-f"}, containers...)
	if out := shell.Run("docker", args...); !out.SucceededOrLog() {
		return fmt.Errorf("could not force delete containers %s: %w",
			strings.Join(containers, ", "), out.Error)
	}
	return nil
}

// deleteContainerImagesRegex deletes all images matching the given regex
func deleteContainerImagesRegex(regex string) error {
	cmdArgs := fmt.Sprintf(`images | grep "%s" | awk '{print $3}' | xargs docker rmi -f`, regex)
	if out := shell.RunShell("docker", cmdArgs); !out.Succeeded() {
		if strings.Contains(out.StderrString(), "requires at least 1 argument") {
			logging.Debug("found no images matching regex " + regex)
			return nil
		}
		out.SucceededOrLog()
		return fmt.Errorf("could not delete images matching regex %s: %w", regex, out.Error)
	}
	return nil
}
