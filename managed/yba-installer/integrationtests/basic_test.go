//go:build integration
// +build integration

package integrationtests

import (
	"fmt"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/integrationtests/testutils"
)

func TestInstall(t *testing.T) {
	// Setup
	mgr := Initialize(t)
	port := testutils.GetNextPort(t)
	version := testutils.GetVersion(t)
	config := TestConfig{
		Manager: mgr,
		Config: NewYBAIConfigBuilder().
			WithPlatformPort(port).
			WithTargetVersion(version).
			WithCustomCerts(false).
			Build(),
	}

	// Run install workflow
	runInstallWorkflow(t, config)
}

func TestUpgrade(t *testing.T) {
	// Setup
	mgr := Initialize(t)
	port := testutils.GetNextPort(t)
	targetVersion := testutils.GetVersion(t)
	config := TestConfig{
		Manager: mgr,
		Config: NewYBAIConfigBuilder().
			WithPlatformPort(port).
			WithTargetVersion(targetVersion).
			WithCustomCerts(false).
			Build(),
	}

	// TODO: This is broken
	fmt.Println(config)
	// Run upgrade workflow
	//runUpgradeWorkflow(t, config)
}
