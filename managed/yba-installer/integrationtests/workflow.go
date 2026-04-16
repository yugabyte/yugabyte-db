package integrationtests

import (
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/containertest"
)

// Workflow implements a standard yba installer workflow that integration tests can further leverage.
// There are 2 main workflows:
//  1. The upgrade workflow. An initial install with the provided config will be installed.
//     some basic workflows will be run to validate the install. From there, a yba upgrade will be
//     performed and then the status will be validated.
//  2. The install workflow. An initial install with the provided config will be installed.
//     some basic workflows will be run to validate the install.
//
// TODO: implement "additional workflows".
// After the initial phase above, tests will be able to optionally perform the additional workflows:
//   - reconfigure yba installer

func runInstallWorkflow(t *testing.T, config TestConfig) {
	mgr := config.Manager

	ctrRef := SetupContainer(t, mgr, config.Config.PlatformPort, config.Config.TargetVersion, containerTag)
	config = config.SetContainer(ctrRef)

	// Install YBA
	installAndValidateYBA(t, mgr, ctrRef)

	configValidate(t, mgr, ctrRef, config.InstallConfValidator)
	stateValidate(t, mgr, ctrRef, config.InstallStateValidator)
}

func runUpgradeWorkflow(t *testing.T, config TestConfig) {
	mgr := config.Manager

	ctrRef := SetupContainer(t, mgr, config.Config.PlatformPort, config.Config.TargetVersion, containerTag)
	config = config.SetContainer(ctrRef)

	// Install YBA
	installAndValidateYBA(t, mgr, ctrRef)
	configValidate(t, mgr, ctrRef, config.InstallConfValidator)
	stateValidate(t, mgr, ctrRef, config.InstallStateValidator)

	// Upgrade YBA
	// TODO: we need to get the latest version from the metadata service, add that to the container,
	// and then use that version to upgrade YBA.
	if output := mgr.Exec(ctrRef, "/yba_installer/yba-ctl", "upgrade", "-f"); !output.Succeeded() {
		t.Fatalf("failed to upgrade YBA: %v", output.StderrString())
	}

	configValidate(t, mgr, ctrRef, config.UpgradeConfValidator)
	stateValidate(t, mgr, ctrRef, config.UpgradeStateValidator)
}

func stateValidate(t *testing.T, mgr containertest.Manager, ctrRef containertest.ContainerRef, validator StateValidator) {
	t.Helper()
	if validator != nil {
		t.Log("Validating YBA state...")
		state := getYbaInstallerStatefile(t, mgr, ctrRef)
		if err := validator(state); err != nil {
			t.Fatalf("YBA state validation failed: %v", err)
		}
	} else {
		t.Log("No state validation function provided, skipping state validation.")
	}
}

func configValidate(t *testing.T, mgr containertest.Manager, ctrRef containertest.ContainerRef, validator ConfigValidator) {
	t.Helper()
	if validator != nil {
		t.Log("Validating YBA config...")
		ycfg := getYbaInstallerConfig(t, mgr, ctrRef)
		if err := validator(ycfg); err != nil {
			t.Fatalf("YBA config validation failed: %v", err)
		}
	} else {
		t.Log("No config validation function provided, skipping config validation.")
	}
}
