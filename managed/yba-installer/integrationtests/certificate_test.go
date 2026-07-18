//go:build integration
// +build integration

package integrationtests

import (
	"path/filepath"
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/integrationtests/testutils"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common"
)

func TestSelfSignedCertInstall(t *testing.T) {
	// Setup
	mgr := Initialize(t)
	port := testutils.GetNextPort(t)
	version := testutils.GetVersion(t)
	t.Log("got version and port")
	ctrInfo := SetupContainer(t, mgr, port, version, containerTag)
	t.Log("container setup complete")

	// Test
	t.Log("starting YBA installation with self-signed certs")
	installAndValidateYBA(t, mgr, ctrInfo)
	ycfg := getYbaInstallerConfig(t, mgr, ctrInfo)
	if ycfg["server_cert_path"] != "" || ycfg["server_key_path"] != "" {
		t.Fatalf("expected self-signed cert paths to be empty, got: %v", ycfg)
	}

	state := getYbaInstallerStatefile(t, mgr, ctrInfo)
	if !state.Config.SelfSignedCert {
		t.Fatalf("expected self-signed cert to be true, got: %v", state.Config.SelfSignedCert)
	}
}

func TestSelfSignedCertsToCustomCerts(t *testing.T) {
	// Setup
	mgr := Initialize(t)
	port := testutils.GetNextPort(t)
	version := testutils.GetVersion(t)
	ctrInfo := SetupContainer(t, mgr, port, version, containerTag)

	// Test
	installAndValidateYBA(t, mgr, ctrInfo)
	ycfg := getYbaInstallerConfig(t, mgr, ctrInfo)
	if ycfg["server_cert_path"] != "" || ycfg["server_key_path"] != "" {
		t.Fatalf("expected self-signed cert paths to be empty, got: %v", ycfg)
	}

	state := getYbaInstallerStatefile(t, mgr, ctrInfo)
	if !state.Config.SelfSignedCert {
		t.Fatalf("expected self-signed cert to be true, got: %v", state.Config.SelfSignedCert)
	}

	// Update to custom certs and run reconfigure
	certDir := testutils.CustomCerts(testWorkingDir)
	localKey := filepath.Join(certDir, common.ServerKeyPath)
	localCert := filepath.Join(certDir, common.ServerCertPath)
	copyCertsAndUpdateConfig(t, mgr, ctrInfo, localCert, localKey)

	if out := mgr.Exec(ctrInfo, "yba-ctl", "reconfigure"); !out.Succeeded() {
		t.Fatalf("failed to reconfigure YBA with custom certs: %v", out.StderrString())
	}

	state = getYbaInstallerStatefile(t, mgr, ctrInfo)
	if state.Config.SelfSignedCert {
		t.Fatalf("expected self-signed cert to be false, got: %v", state.Config.SelfSignedCert)
	}
}
