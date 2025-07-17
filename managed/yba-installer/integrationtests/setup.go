package integrationtests

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/integrationtests/testutils"
	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/containertest"
)

var (
	testWorkingDir = "/tmp/yba-installer-integration-tests"
	containerTag   = "yba-installer-test:latest"
)

var downloadMutex = sync.Mutex{}

func Initialize(tb testing.TB) containertest.Manager {
	tb.Helper()
	mgr, err := containertest.NewManager()
	if err != nil {
		tb.Fatalf("failed to create container manager: %v", err)
	}
	buildImages(tb, mgr)

	return mgr
}

func buildImages(tb testing.TB, mgr containertest.Manager) {
	tb.Helper()
	// Build the test image
	dockerFile := filepath.Join(testutils.GetTopDir(), "integrationtests", "resources", "Dockerfile")
	if err := mgr.BuildImage(dockerFile, "yba-installer-test", "latest"); err != nil {
		tb.Fatalf("failed to build test image: %v", err)
	}
	tb.Log("Test image built successfully")
}

func SetupContainer(tb testing.TB, mgr containertest.Manager, port int, version string, image string) containertest.ContainerRef {
	tb.Helper()
	ctrName := tb.Name() + "-container"

	// Only download in 1 thread
	downloadMutex.Lock()
	tgzPath := downloadYBAInstallerFull(tb, version)
	ybaiPath := extractTgzPath(tb, tgzPath, version)
	licensePath := downloadLicense(tb)
	downloadMutex.Unlock()

	// Create the config
	cfg := containertest.NewConfig()
	cfg.BaseImage = image
	absPath, err := filepath.Abs(ybaiPath) // Ensure the path is absolute
	if err != nil {
		tb.Fatalf("failed to get absolute path for %s: %v", ybaiPath, err)
	}
	licenseAbsPath, err := filepath.Abs(licensePath) // Ensure the path is absolute
	if err != nil {
		tb.Fatalf("failed to get absolute path for %s: %v", licensePath, err)
	}
	cfg = cfg.AddVolume(absPath, "/yba_installer", false)
	cfg = cfg.AddVolume(licenseAbsPath, "/yba.lic", false)

	cfg = cfg.AddPort(port, 443) // YBA web UI port

	// Start the container
	tb.Logf("Starting container %s with YBA version %s", ctrName, version)
	ctr := mgr.Start(ctrName, cfg)
	for range 30 {
		running, err := mgr.IsContainerRunning(ctr)
		if err != nil {
			tb.Fatalf("failed to check if container %s is running: %v", ctrName, err)
		}
		if running {
			break
		}
		tb.Logf("Waiting for container %s to start...", ctrName)
		time.Sleep(1 * time.Second)
	}
	running, err := mgr.IsContainerRunning(ctr)
	if err != nil || !running {
		tb.Fatalf("container %s is not running after start. error: %v", ctrName, err)
	}

	// Set cleanup to stop container
	tb.Cleanup(func() {
		if err := mgr.Stop(ctr); err != nil {
			tb.Errorf("failed to stop container %s: %v", ctrName, err)
		}
	})
	return ctr
}

func downloadYBAInstallerFull(tb testing.TB, version string) string {
	tb.Helper()
	if err := os.MkdirAll(testWorkingDir, 0775); err != nil {
		tb.Fatalf("failed to create test working directory %s: %v", testWorkingDir, err)
	}
	outPath := filepath.Join(testWorkingDir, fmt.Sprintf("yba_installer_full-%s-centos-x86_64.tar.gz", version))
	if _, err := os.Stat(outPath); os.IsNotExist(err) {
		bucket := "releases.yugabyte.com"
		key := fmt.Sprintf("%s/yba_installer_full-%s-centos-x86_64.tar.gz", version, version)
		testutils.DownloadS3Package(tb, bucket, key, outPath)
	} else {
		tb.Log("skipping download")
	}
	return outPath
}

func extractTgzPath(tb testing.TB, tgzPath, version string) string {
	tb.Helper()
	extractPath := filepath.Join(testWorkingDir, fmt.Sprintf("yba_installer_full-%s", version))
	if _, err := os.Stat(extractPath); os.IsNotExist(err) {
		testutils.ExtractTgzPackage(tb, tgzPath, filepath.Dir(extractPath))
	} else {
		tb.Log("skipping extraction, already exists")
	}
	return extractPath
}

func downloadLicense(tb testing.TB) string {
	tb.Helper()
	licensePath := filepath.Join(testWorkingDir, "yba.lic")
	if _, err := os.Stat(licensePath); os.IsNotExist(err) {
		key := "yba_installer/yugabyte_anywhere.lic"
		testutils.DownloadS3Package(tb, "releases.yugabyte.com", key, licensePath)
	} else {
		tb.Log("skipping license download")
	}
	return licensePath
}
