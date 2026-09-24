// Copyright (c) YugabyteDB, Inc.

package mountephemeraldrives

import (
	"bytes"
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func mountEphemeralDrivesModulePath(t *testing.T) string {
	t.Helper()
	if projectDir := os.Getenv("PROJECT_DIR"); projectDir != "" {
		return filepath.Join(
			projectDir,
			"resources",
			"ynp",
			"modules",
			"provision",
			"mount_ephemeral_drives",
		)
	}
	path, err := filepath.Abs(
		filepath.Join(
			"..",
			"..",
			"..",
			"..",
			"resources",
			"ynp",
			"modules",
			"provision",
			"mount_ephemeral_drives",
		),
	)
	if err != nil {
		t.Fatalf("resolve module path: %v", err)
	}
	return path
}

func writeTestFile(t *testing.T, path string, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("create directory for %s: %v", path, err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func addNVMeController(
	t *testing.T,
	sysClassNVMe string,
	controller string,
	model string,
) {
	t.Helper()
	writeTestFile(t, filepath.Join(sysClassNVMe, controller, "model"), model+"\n")
	writeTestFile(
		t,
		filepath.Join(sysClassNVMe, controller, "device", "vendor"),
		"0x1414\n",
	)
}

func addNVMeNamespace(
	t *testing.T,
	devRoot string,
	sysClassNVMe string,
	controller string,
	device string,
	namespaceID string,
) string {
	t.Helper()
	devicePath := filepath.Join(devRoot, device)
	writeTestFile(t, devicePath, "")
	writeTestFile(
		t,
		filepath.Join(sysClassNVMe, controller, device, "nsid"),
		namespaceID+"\n",
	)
	return devicePath
}

func runAzureDiskResolver(
	devRoot string,
	sysClassNVMe string,
	sysClassBlock string,
	helperPath string,
	lun string,
	nvmeIDCommand ...string,
) (string, string, error) {
	command := "disabled"
	if len(nvmeIDCommand) > 0 {
		command = nvmeIDCommand[0]
	}
	script := `
set -euo pipefail
DEV_ROOT=$1
AZURE_DISK_ROOT=$DEV_ROOT/disk/azure
SYS_CLASS_NVME=$2
SYS_CLASS_BLOCK=$3
AZURE_NVME_ID_COMMAND=$6
source "$4"
resolve_azure_data_disk "$5"
printf '%s\n' "$AZURE_DATA_DISK"
`
	cmd := exec.Command(
		"bash",
		"-c",
		script,
		"azure-disk-test",
		devRoot,
		sysClassNVMe,
		sysClassBlock,
		helperPath,
		lun,
		command,
	)
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	return strings.TrimSpace(stdout.String()), strings.TrimSpace(stderr.String()), err
}

func TestResolveLegacyAzureSCSIDiskByLUN(t *testing.T) {
	root := t.TempDir()
	devRoot := filepath.Join(root, "dev")
	sysClassNVMe := filepath.Join(root, "sys", "class", "nvme")
	sysClassBlock := filepath.Join(root, "sys", "class", "block")
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)

	devicePath := filepath.Join(devRoot, "sdc")
	writeTestFile(t, devicePath, "")
	lunPath := filepath.Join(devRoot, "disk", "azure", "scsi1", "lun0")
	if err := os.MkdirAll(filepath.Dir(lunPath), 0o755); err != nil {
		t.Fatalf("create legacy Azure link directory: %v", err)
	}
	if err := os.Symlink(devicePath, lunPath); err != nil {
		t.Fatalf("create legacy Azure LUN link: %v", err)
	}

	output, stderr, err := runAzureDiskResolver(
		devRoot,
		sysClassNVMe,
		sysClassBlock,
		helperPath,
		"0",
	)
	if err != nil {
		t.Fatalf(
			"resolve legacy Azure SCSI disk: %v\nstdout: %s\nstderr: %s",
			err,
			output,
			stderr,
		)
	}
	if output != devicePath {
		t.Fatalf("resolved device = %q, want %q", output, devicePath)
	}
}

func TestResolveAzureNVMeDataDiskExcludesLocalDisk(t *testing.T) {
	root := t.TempDir()
	devRoot := filepath.Join(root, "dev")
	sysClassNVMe := filepath.Join(root, "sys", "class", "nvme")
	sysClassBlock := filepath.Join(root, "sys", "class", "block")
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)

	addNVMeController(t, sysClassNVMe, "nvme0", "MSFT NVMe Accelerator v1.0")
	osDisk := addNVMeNamespace(t, devRoot, sysClassNVMe, "nvme0", "nvme0n1", "1")
	dataDisk := addNVMeNamespace(t, devRoot, sysClassNVMe, "nvme0", "nvme0n2", "2")

	addNVMeController(t, sysClassNVMe, "nvme1", "Microsoft NVMe Direct Disk v2")
	localDisk := addNVMeNamespace(t, devRoot, sysClassNVMe, "nvme1", "nvme1n1", "1")

	for link, target := range map[string]string{
		filepath.Join(devRoot, "disk", "azure", "os"):                     osDisk,
		filepath.Join(devRoot, "disk", "azure", "local", "by-index", "1"): localDisk,
	} {
		if err := os.MkdirAll(filepath.Dir(link), 0o755); err != nil {
			t.Fatalf("create Azure link directory: %v", err)
		}
		if err := os.Symlink(target, link); err != nil {
			t.Fatalf("create Azure link %s: %v", link, err)
		}
	}

	output, stderr, err := runAzureDiskResolver(
		devRoot,
		sysClassNVMe,
		sysClassBlock,
		helperPath,
		"0",
	)
	if err != nil {
		t.Fatalf(
			"resolve Azure NVMe data disk: %v\nstdout: %s\nstderr: %s",
			err,
			output,
			stderr,
		)
	}
	if output != dataDisk {
		t.Fatalf(
			"resolved device = %q, want data disk %q (local disk is %q)",
			output,
			dataDisk,
			localDisk,
		)
	}
}

func TestAzureNVMeResolverFailsWithOnlyLocalDisk(t *testing.T) {
	root := t.TempDir()
	devRoot := filepath.Join(root, "dev")
	sysClassNVMe := filepath.Join(root, "sys", "class", "nvme")
	sysClassBlock := filepath.Join(root, "sys", "class", "block")
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)

	addNVMeController(t, sysClassNVMe, "nvme1", "Microsoft NVMe Direct Disk v2")
	localDisk := addNVMeNamespace(t, devRoot, sysClassNVMe, "nvme1", "nvme1n1", "1")
	// Even a stale or incorrect by-LUN link must not override the NVMe
	// controller's positive identification of this device as local storage.
	bogusDataLink := filepath.Join(devRoot, "disk", "azure", "data", "by-lun", "0")
	if err := os.MkdirAll(filepath.Dir(bogusDataLink), 0o755); err != nil {
		t.Fatalf("create Azure data link directory: %v", err)
	}
	if err := os.Symlink(localDisk, bogusDataLink); err != nil {
		t.Fatalf("create bogus Azure data link: %v", err)
	}
	fakeNVMeID := filepath.Join(root, "fake-azure-nvme-id")
	writeTestFile(
		t,
		fakeNVMeID,
		"#!/bin/bash\n"+
			"echo AZURE_DISK_TYPE=data\n"+
			"echo AZURE_DISK_LUN=0\n",
	)
	if err := os.Chmod(fakeNVMeID, 0o755); err != nil {
		t.Fatalf("make fake azure-nvme-id executable: %v", err)
	}

	output, stderr, err := runAzureDiskResolver(
		devRoot,
		sysClassNVMe,
		sysClassBlock,
		helperPath,
		"0",
		fakeNVMeID,
	)
	if err == nil {
		t.Fatalf(
			"resolver unexpectedly selected a local Azure disk: stdout: %s, stderr: %s",
			output,
			stderr,
		)
	}
	if !strings.Contains(stderr, "Unable to identify an Azure data disk for LUN 0") {
		t.Fatalf(
			"unexpected resolver failure: %v\nstdout: %s\nstderr: %s",
			err,
			output,
			stderr,
		)
	}
}

func TestRenderedScriptKeepsAzureDiskIdentity(t *testing.T) {
	modulePath := mountEphemeralDrivesModulePath(t)
	module := NewMountEphemeralDrive(filepath.Dir(modulePath))
	values := map[string]any{
		"cloud_type":       "azu",
		"device_paths":     "sdc",
		"mount_paths":      "/mnt/d0",
		"disk_lun_indexes": "0",
		"imdsv2required":   "False",
	}

	rendered, err := module.RenderTemplates(context.Background(), values)
	if err != nil {
		t.Fatalf("render mount script: %v", err)
	}
	script := rendered.RenderedContent("run")
	for _, expected := range []string{
		`readonly MOUNT_PATHS_B64="L21udC9kMA=="`,
		`device_path=$e`,
		`elif [[ $CLOUD_TYPE == "aws" && "$DRIVE_SCHEME" == "nvme" ]]; then`,
		`validate_azure_data_disk "$canonical_device_path"`,
		`replace_fstab_mount_entry`,
		`mount "$mount_path"`,
		`MSFT NVMe Accelerator v1.0`,
	} {
		if !strings.Contains(script, expected) {
			t.Fatalf("rendered script does not contain %q", expected)
		}
	}
	if strings.Contains(script, `readonly MOUNT_PATHS="/mnt/d0"`) {
		t.Fatal("rendered script contains an unencoded mount-path assignment")
	}
	formatOffset := strings.Index(script, "/sbin/mkfs.xfs $device_path -f")
	if formatOffset < 0 {
		t.Fatal("rendered script does not contain the XFS formatting command")
	}
	if guardOffset := strings.LastIndex(
		script[:formatOffset],
		`validate_azure_data_disk "$canonical_device_path"`,
	); guardOffset < 0 {
		t.Fatal("rendered script does not revalidate the Azure LUN before formatting")
	}

	scriptPath := filepath.Join(t.TempDir(), "mount_ephemeral_drives.sh")
	if err := os.WriteFile(scriptPath, []byte(script), 0o700); err != nil {
		t.Fatalf("write rendered script: %v", err)
	}
	if output, err := exec.Command("bash", "-n", scriptPath).CombinedOutput(); err != nil {
		t.Fatalf("rendered script has invalid shell syntax: %v\n%s", err, output)
	}
}

func TestReplaceFstabMountEntryUpdatesStaleUUID(t *testing.T) {
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)
	fstabPath := filepath.Join(t.TempDir(), "fstab")
	writeTestFile(
		t,
		fstabPath,
		"# unrelated entry\n"+
			"UUID=root / xfs defaults 0 1\n"+
			"#/mnt/d0\n"+
			"UUID=stale /mnt/d0 xfs defaults,nofail 0 2\n"+
			"/dev/nvme9n9 /mnt/d0 xfs defaults 0 2\n",
	)

	script := `
set -euo pipefail
source "$1"
replace_fstab_mount_entry "$2" /mnt/d0 current-uuid xfs defaults,noatime,nofail
`
	if output, err := exec.Command(
		"bash",
		"-c",
		script,
		"fstab-test",
		helperPath,
		fstabPath,
	).CombinedOutput(); err != nil {
		t.Fatalf("replace fstab entry: %v\n%s", err, output)
	}

	contentBytes, err := os.ReadFile(fstabPath)
	if err != nil {
		t.Fatalf("read updated fstab: %v", err)
	}
	content := string(contentBytes)
	expectedEntry := "UUID=current-uuid /mnt/d0 xfs defaults,noatime,nofail 0 2"
	if strings.Count(content, "#/mnt/d0") != 1 ||
		strings.Count(content, expectedEntry) != 1 {
		t.Fatalf("updated fstab does not contain exactly one current entry:\n%s", content)
	}
	if strings.Contains(content, "UUID=stale") || strings.Contains(content, "/dev/nvme9n9") {
		t.Fatalf("updated fstab retained a stale mount entry:\n%s", content)
	}
	if !strings.Contains(content, "UUID=root / xfs defaults 0 1") {
		t.Fatalf("updated fstab removed an unrelated entry:\n%s", content)
	}
}

func TestValidateMountPathsRejectsUnsafeTargets(t *testing.T) {
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)
	tests := []struct {
		name          string
		mountPath     string
		fstab         string
		expectedError string
	}{
		{
			name:          "protected root",
			mountPath:     "/",
			fstab:         "UUID=root / xfs defaults 0 1\n",
			expectedError: "Refusing protected mount path",
		},
		{
			name:          "shell expression",
			mountPath:     "/mnt/d0;$(touch /tmp/unsafe)",
			fstab:         "",
			expectedError: "Unsafe mount path",
		},
		{
			name:          "non-YBA fstab owner",
			mountPath:     "/data",
			fstab:         "UUID=external /data xfs defaults 0 2\n",
			expectedError: "Refusing to replace a non-YBA fstab entry",
		},
		{
			name:          "system descendant",
			mountPath:     "/home/yugabyte",
			fstab:         "",
			expectedError: "outside the approved /data and /mnt roots",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			fstabPath := filepath.Join(t.TempDir(), "fstab")
			writeTestFile(t, fstabPath, test.fstab)
			script := `
set -euo pipefail
source "$1"
mounts=("$2")
FSTAB_PATH=$3
validate_mount_paths
`
			output, err := exec.Command(
				"bash",
				"-c",
				script,
				"mount-path-test",
				helperPath,
				test.mountPath,
				fstabPath,
			).CombinedOutput()
			if err == nil {
				t.Fatalf("unsafe mount path %q was accepted", test.mountPath)
			}
			if !strings.Contains(string(output), test.expectedError) {
				t.Fatalf("unexpected validation error: %v\n%s", err, output)
			}
		})
	}
}

func TestValidateMountPathsRejectsOverlappingTargets(t *testing.T) {
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"azure_disk_utils.sh.j2",
	)
	fstabPath := filepath.Join(t.TempDir(), "fstab")
	writeTestFile(t, fstabPath, "")
	script := `
set -euo pipefail
source "$1"
mounts=("/mnt/data" "/mnt/data/logs")
FSTAB_PATH=$2
validate_mount_paths
`
	output, err := exec.Command(
		"bash",
		"-c",
		script,
		"overlapping-mount-test",
		helperPath,
		fstabPath,
	).CombinedOutput()
	if err == nil {
		t.Fatal("overlapping mount paths were accepted")
	}
	if !strings.Contains(string(output), "Overlapping mount paths are not allowed") {
		t.Fatalf("unexpected validation error: %v\n%s", err, output)
	}
}

func TestAzureLUNMismatchMessageIncludesLUN(t *testing.T) {
	root := t.TempDir()
	output, stderr, err := runAzureDiskResolver(
		filepath.Join(root, "dev"),
		filepath.Join(root, "sys", "class", "nvme"),
		filepath.Join(root, "sys", "class", "block"),
		filepath.Join(
			mountEphemeralDrivesModulePath(t),
			"templates",
			"azure_disk_utils.sh.j2",
		),
		"7",
	)
	if err == nil {
		t.Fatal("resolver unexpectedly found Azure LUN 7")
	}
	if !strings.Contains(stderr, "LUN 7") {
		t.Fatalf(
			"resolver output does not identify the missing LUN: stdout: %s, stderr: %s",
			output,
			stderr,
		)
	}
}
