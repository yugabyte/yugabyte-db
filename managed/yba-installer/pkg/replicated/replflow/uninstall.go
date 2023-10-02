package replflow

import (
	"fmt"
	"strings"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/replicated/replicatedctl"
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
	// Get the root install dir for deletion later
	replCtl := replicatedctl.New(replicatedctl.Config{})
	config, err := replCtl.AppConfigExport()
	if err != nil {
		logging.Fatal("failed to export replicated app config: " + err.Error())
	}
	rootDir, ok := config.ConfigEntries[replicatedctl.StoragePathKey]
	if !ok {
		logging.Warn("replicated config had no config entry " + replicatedctl.StoragePathKey +
			". Skipping delete of root install directory.")
	}

	// Stop and disable the replicated services
	logging.Info("stopping replicated system services")
	if err := systemd.Stop(replicatedServices...); err != nil {
		return fmt.Errorf("uninstall failed to stop replicated: %w", err)
	}
	if err := systemd.Disable(replicatedServices...); err != nil {
		return fmt.Errorf("uninstall failed to disable replicated: %w", err)
	}

	logging.Info("stopping replicated containers")
	// Stop the 2 containers
	for _, ctr := range replicatedStopContainers {
		if err := stopContainer(ctr); err != nil {
			return fmt.Errorf("uninstall failed to stop %s: %w", ctr, err)
		}
	}

	logging.Info("deleting all replicated containers")
	// Force remove all containers
	if err := forceDeleteContainer(replicatedDeleteContainers...); err != nil {
		return fmt.Errorf("uninstall failed to force delete containers: %w", err)
	}

	logging.Info("deleting replicated images")
	for _, rgx := range replicatedImageRegexes {
		if err := deleteContainerImagesRegex(rgx); err != nil {
			return fmt.Errorf("uninstall failed to delete images matching %s: %w", rgx, err)
		}
	}

	logging.Info("removing replicated files")
	for _, file := range replicatedFiles {
		if out := shell.RunShell("rm", "-r", file); !out.Succeeded() {
			if strings.Contains(out.StderrString(), "No such file or directory") {
				logging.Debug("file " + file + " already deleted")
				continue
			}
			out.SucceededOrLog()
			return fmt.Errorf("failed to delete %s: %w", file, out.Error)
		}
	}

	if err := systemd.DaemonReload(); err != nil {
		return fmt.Errorf("uninstall failed to reload systemd: %w", err)
	}

	if err := common.RemoveAll(rootDir.Value); err != nil {
		return fmt.Errorf("failed to delete replicated install directory %s: %w", rootDir.Value, err)
	}
	logging.Info("Uninstalled replicated")
	return nil
}
