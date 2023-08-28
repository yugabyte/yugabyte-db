package replflow

import (
	"fmt"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/systemd"
)

var (
	replicatedServices         = []string{"replicated", "replicated-ui", "replicated-operator"}
	replicatedStopContainers   = []string{"replicated-premkit", "replicated-statsd"}
	replicatedDeleteContainers = []string{"replicated", "replicated-ui", "replicated-operator",
		"replicated-premkit", "retraced-api", "retraced-processor", "retraced-cron", "retraced-nsqd",
		"retraced-postgres"}
	replicatedImageRegexes = []string{`quay\.io/replicated`,
		`registry\.replicated\.com/library/retraced`}
	replicatedFiles = []string{"/etc/default/replicated*", "/etc/init.d/replicated*",
		"/etc/init/replicated*", "/etc/replicated", "/etc/replicated.alias",
		"/etc/sysconfig/replicated*", "/etc/systemd/system/multi-user.target.wants/replicated*",
		"/etc/systemd/system/replicated*", "/run/replicated*", "/usr/local/bin/replicated*",
		"/var/lib/replicated*", "/var/log/upstart/replicated*"}
)

func Uninstall() error {
	// Stop and disable the replicated services
	logging.Debug("stopping replicated system services")
	if err := systemd.Stop(replicatedServices...); err != nil {
		return fmt.Errorf("uninstall failed to stop replicated: %w", err)
	}
	if err := systemd.Disable(replicatedServices...); err != nil {
		return fmt.Errorf("uninstall failed to disable replicated: %w", err)
	}

	logging.Debug("stopping replicated containers")
	// Stop the 2 containers
	for _, ctr := range replicatedStopContainers {
		if err := stopContainer(ctr); err != nil {
			return fmt.Errorf("uninstall failed to stop %s: %w", ctr, err)
		}
	}

	logging.Debug("deleting all replicated containers")
	// Force remove all containers
	if err := forceDeleteContainer(replicatedDeleteContainers...); err != nil {
		return fmt.Errorf("uninstall failed to force delete containers: %w", err)
	}

	logging.Debug("deleting replicated images")
	for _, rgx := range replicatedImageRegexes {
		if err := deleteContainerImagesRegex(rgx); err != nil {
			return fmt.Errorf("uninstall failed to delete images matching %s: %w", rgx, err)
		}
	}

	logging.Debug("removing replicated files")
	args := append([]string{"-rf"}, replicatedFiles...)
	if out := shell.Run("rm", args...); !out.SucceededOrLog() {
		return fmt.Errorf("uninstall failed to delete replicated files: %w", out.Error)
	}

	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("uninstall failed to reload systemd: %w", err)
	}
	return nil
}
