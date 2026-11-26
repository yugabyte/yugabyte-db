// Copyright (c) YugabyteDB, Inc.

package mountephemeraldrives

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// These helpers exercise production shell that requires GNU coreutils
// (readlink -e, realpath -m, chmod --reference). Skip on hosts that lack them
// (notably macOS), since the scripts only run on Linux DB nodes.
func requireGNUCoreutils(t *testing.T) {
	t.Helper()
	if runtime.GOOS != "linux" {
		t.Skipf(
			"mount_ephemeral_drives helpers require GNU coreutils on linux; got %s",
			runtime.GOOS,
		)
	}
}

func mountEphemeralDrivesModulePath(t *testing.T) string {
	t.Helper()
	requireGNUCoreutils(t)
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

// azure_disk_utils.sh.j2 uses GNU realpath -m, readlink -e, and chmod --reference.
// Skip helper-driven tests on platforms that only ship BSD/macOS userland.
func requireGNUUserland(t *testing.T) {
	t.Helper()
	if err := exec.Command("realpath", "-m", "/").Run(); err != nil {
		t.Skip("requires GNU realpath -m (Linux); skipping on this host")
	}
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
	t *testing.T,
	devRoot string,
	sysClassNVMe string,
	sysClassBlock string,
	helperPath string,
	lun string,
	nvmeIDCommand ...string,
) (string, string, error) {
	t.Helper()
	requireGNUUserland(t)
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
		t,
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
		t,
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
		t,
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
	requireGNUUserland(t)
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
	requireGNUUserland(t)
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
	requireGNUUserland(t)
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
		t,
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

// ociDisk is a disk and its lsblk -nr -o NAME,MOUNTPOINT output. signature is what blkid -p
// prints for it; a disk without one is blank. uuid is its filesystem UUID. blkidError makes blkid
// fail to open the disk, and lsblkFails makes lsblk fail on it.
type ociDisk struct {
	name       string
	tree       string
	signature  string
	uuid       string
	blkidError bool
	lsblkFails bool
}

// ociFstab returns a YBA-written fstab that mounts each given UUID at /mnt/d0, /mnt/d1, ...
// An empty UUID leaves that mount path out.
func ociFstab(uuids ...string) string {
	fstab := "UUID=root-uuid / xfs defaults 0 0\n"
	for i, uuid := range uuids {
		if uuid != "" {
			fstab += fmt.Sprintf("#/mnt/d%[1]d\nUUID=%[2]s /mnt/d%[1]d xfs defaults 0 2\n", i, uuid)
		}
	}
	return fstab
}

func ociDataDisk(name string) ociDisk {
	return ociDisk{name: name, tree: name + " \n"}
}

func ociBootDisk(name string) ociDisk {
	return ociDisk{
		name: name,
		tree: fmt.Sprintf("%[1]s \n%[1]s1 \n%[1]s2 /boot/efi\n%[1]s3 /boot\n%[1]s4 /\n", name),
	}
}

func writeTestExecutable(t *testing.T, path string, content string) {
	t.Helper()
	writeTestFile(t, path, content)
	if err := os.Chmod(path, 0o755); err != nil {
		t.Fatalf("make %s executable: %v", path, err)
	}
}

// runOCIDiskResolver runs get_oci_drives with a fake lsblk that lists the given disks in order,
// a fake blkid, and the given fstab. It returns the resolved drives, the full stdout and stderr.
func runOCIDiskResolver(
	t *testing.T,
	disks []ociDisk,
	requested string,
	mountPaths string,
	fstab string,
) (string, string, string, error) {
	t.Helper()
	helperPath := filepath.Join(
		mountEphemeralDrivesModulePath(t),
		"templates",
		"oci_disk_utils.sh.j2",
	)
	root := t.TempDir()
	fixtures := filepath.Join(root, "lsblk")
	diskList := "sr0 rom\n"
	for _, disk := range disks {
		diskList += disk.name + " disk\n"
		writeTestFile(t, filepath.Join(fixtures, "tree", disk.name), disk.tree)
		if disk.lsblkFails {
			writeTestFile(t, filepath.Join(fixtures, "lsblk-fails", disk.name), "")
		}
		if disk.signature != "" {
			writeTestFile(t, filepath.Join(fixtures, "blkid", disk.name), disk.signature+"\n")
		}
		if disk.blkidError {
			writeTestFile(t, filepath.Join(fixtures, "blkid-error", disk.name), "")
		}
		if disk.uuid != "" {
			writeTestFile(t, filepath.Join(fixtures, "uuid", disk.name), disk.uuid)
		}
	}
	fstabPath := filepath.Join(root, "fstab")
	writeTestFile(t, fstabPath, fstab)
	writeTestFile(t, filepath.Join(fixtures, "disks"), diskList)
	binDir := filepath.Join(root, "bin")
	writeTestExecutable(
		t,
		filepath.Join(binDir, "lsblk"),
		"#!/bin/bash\n"+
			"if [[ $1 == -dnr ]]; then cat \"$LSBLK_FIXTURES/disks\"; exit; fi\n"+
			"device=${!#}\n"+
			"name=${device##*/}\n"+
			"if [[ -e $LSBLK_FIXTURES/lsblk-fails/$name ]]; then\n"+
			"  echo >&2 \"lsblk: $device: failed\"; exit 1\n"+
			"fi\n"+
			"cat \"$LSBLK_FIXTURES/tree/$name\" 2>/dev/null || exit 32\n",
	)
	writeTestExecutable(
		t,
		filepath.Join(binDir, "blkid"),
		"#!/bin/bash\n"+
			"if [[ $1 == -c && $3 == -t ]]; then\n"+
			"  grep -lx \"${4#UUID=}\" \"$LSBLK_FIXTURES\"/uuid/* 2>/dev/null | sed 's|.*/|/dev/|'\n"+
			"  exit\n"+
			"fi\n"+
			"[[ $1 == -p ]] || exit 4\n"+
			"name=${2##*/}\n"+
			"if [[ -e $LSBLK_FIXTURES/blkid-error/$name ]]; then\n"+
			"  echo >&2 \"error: $2: Permission denied\"; exit 2\n"+
			"fi\n"+
			"cat \"$LSBLK_FIXTURES/blkid/$name\" 2>/dev/null || exit 2\n",
	)
	script := `
set -euo pipefail
DEV_ROOT=/dev
DEFAULT_SSD_DRIVES=$2
read -r -a mounts <<< "$3"
FSTAB_PATH=$4
source "$1"
get_oci_drives
printf '%s\n' "$get_oci_drives_rv"
`
	cmd := exec.Command(
		"bash", "-c", script, "oci-disk-test", helperPath, requested, mountPaths, fstabPath)
	cmd.Env = append(
		os.Environ(),
		"PATH="+binDir+string(os.PathListSeparator)+os.Getenv("PATH"),
		"LSBLK_FIXTURES="+fixtures,
	)
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	lines := strings.Split(strings.TrimSpace(stdout.String()), "\n")
	return lines[len(lines)-1], stdout.String(), strings.TrimSpace(stderr.String()), err
}

func TestResolveOCIDrives(t *testing.T) {
	tests := []struct {
		name       string
		disks      []ociDisk
		requested  string
		mountPaths string
		fstab      string
		want       string
	}{
		{
			name:       "boot volume is sda",
			disks:      []ociDisk{ociBootDisk("sda"), ociDataDisk("sdb")},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdb",
		},
		{
			name:       "boot volume is sdb",
			disks:      []ociDisk{ociDataDisk("sda"), ociBootDisk("sdb")},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sda",
		},
		{
			name:       "boot volume between data volumes",
			disks:      []ociDisk{ociDataDisk("sda"), ociBootDisk("sdb"), ociDataDisk("sdc")},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			want:       "sda sdc",
		},
		{
			name:       "boot volume is the last requested name",
			disks:      []ociDisk{ociDataDisk("sda"), ociDataDisk("sdb"), ociBootDisk("sdc")},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			want:       "sdb sda",
		},
		{
			name: "LVM boot volume is sdb",
			disks: []ociDisk{
				ociDataDisk("sda"),
				{name: "sdb", tree: "sdb \nsdb1 /boot/efi\nsdb2 /boot\nsdb3 \nocivolume-root /\n"},
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sda",
		},
		{
			name:       "data volume already mounted at a YBA mount path",
			disks:      []ociDisk{{name: "sda", tree: "sda /mnt/d0\n"}, ociBootDisk("sdb")},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sda",
		},
		{
			// The probe loop then rejects the boot volume.
			name:       "disk mounted outside YBA is not used",
			disks:      []ociDisk{{name: "sda", tree: "sda /backup\n"}, ociBootDisk("sdb")},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdb",
		},
		{
			name: "unrequested disk does not replace requested data volumes",
			disks: []ociDisk{
				ociBootDisk("sda"),
				ociDataDisk("sdb"),
				ociDataDisk("sdc"),
				ociDataDisk("sdd"),
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			want:       "sdb sdc",
		},
		{
			name:       "local NVMe drives",
			disks:      []ociDisk{ociBootDisk("sda"), ociDataDisk("sdb"), ociDataDisk("nvme0n1")},
			requested:  "nvme0n1",
			mountPaths: "/mnt/d0",
			want:       "nvme0n1",
		},
		{
			// The probe loop then skips the missing disk, and the mount fails.
			name:       "missing requested disk is kept",
			disks:      []ociDisk{ociBootDisk("sda"), ociDataDisk("sdc")},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdb",
		},
		{
			name: "disk with data is not used",
			disks: []ociDisk{
				{name: "sda", tree: "sda \n", signature: `/dev/sda: TYPE="ext4"`},
				ociBootDisk("sdb"),
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdb",
		},
		{
			name: "blank disk is used over a disk with data",
			disks: []ociDisk{
				{name: "sda", tree: "sda \n", signature: `/dev/sda: TYPE="xfs"`},
				ociBootDisk("sdb"),
				ociDataDisk("sdc"),
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdc",
		},
		{
			name: "disk blkid cannot open is not used",
			disks: []ociDisk{
				{name: "sda", tree: "sda \n", blkidError: true},
				ociBootDisk("sdb"),
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdb",
		},
		{
			name: "disk lsblk fails on is not used",
			disks: []ociDisk{
				{name: "sda", tree: "sda \n", lsblkFails: true},
				ociBootDisk("sdb"),
				ociDataDisk("sdc"),
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdc",
		},
		{
			name: "disk mounted at another YBA mount path is not used",
			disks: []ociDisk{
				{name: "sda", tree: "sda /mnt/d1\n"},
				ociBootDisk("sdb"),
				ociDataDisk("sdc"),
				ociDataDisk("sdd"),
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			want:       "sdd sdc",
		},
		{
			// fstab mounts data volumes with nofail, so they can be left unmounted.
			name: "unmounted data volumes with names swapped",
			disks: []ociDisk{
				ociBootDisk("sda"),
				{name: "sdb", tree: "sdb \n", uuid: "uuid-1"},
				{name: "sdc", tree: "sdc \n", uuid: "uuid-0"},
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			fstab:      ociFstab("uuid-0", "uuid-1"),
			want:       "sdc sdb",
		},
		{
			name: "boot volume is sdb and the second data volume sda",
			disks: []ociDisk{
				{name: "sda", tree: "sda /mnt/d1\n", uuid: "uuid-1"},
				ociBootDisk("sdb"),
				{name: "sdc", tree: "sdc /mnt/d0\n", uuid: "uuid-0"},
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			fstab:      ociFstab("uuid-0", "uuid-1"),
			want:       "sdc sda",
		},
		{
			name: "boot volume is the last requested name with data volumes in fstab",
			disks: []ociDisk{
				{name: "sda", tree: "sda /mnt/d0\n", uuid: "uuid-0"},
				{name: "sdb", tree: "sdb /mnt/d1\n", uuid: "uuid-1"},
				ociBootDisk("sdc"),
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			fstab:      ociFstab("uuid-0", "uuid-1"),
			want:       "sda sdb",
		},
		{
			name:       "fstab entry for a detached volume",
			disks:      []ociDisk{ociBootDisk("sda"), ociDataDisk("sdb"), ociDataDisk("sdc")},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			fstab:      ociFstab("detached-uuid"),
			want:       "sdb sdc",
		},
		{
			name: "fstab entry YBA did not write",
			disks: []ociDisk{
				ociBootDisk("sda"),
				{name: "sdb", tree: "sdb \n", uuid: "uuid-0"},
				{name: "sdc", tree: "sdc \n", uuid: "uuid-1"},
			},
			requested:  "sdb sdc",
			mountPaths: "/mnt/d0 /mnt/d1",
			fstab:      "UUID=uuid-1 /mnt/d0 xfs defaults 0 2\n",
			want:       "sdb sdc",
		},
		{
			name: "disk an earlier run mounted is used over a blank disk",
			disks: []ociDisk{
				ociDataDisk("sda"),
				ociBootDisk("sdb"),
				{name: "sdc", tree: "sdc /mnt/d0\n"},
			},
			requested:  "sdb",
			mountPaths: "/mnt/d0",
			want:       "sdc",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			got, stdout, stderr, err := runOCIDiskResolver(
				t,
				test.disks,
				test.requested,
				test.mountPaths,
				test.fstab,
			)
			if err != nil {
				t.Fatalf("resolve OCI drives: %v\nstdout: %s\nstderr: %s", err, stdout, stderr)
			}
			if got != test.want {
				t.Fatalf("resolved drives = %q, want %q\nstdout: %s", got, test.want, stdout)
			}
		})
	}
}

func TestResolveOCIDrivesRefusesRequestedDisk(t *testing.T) {
	tests := []struct {
		name      string
		disks     []ociDisk
		requested string
		fstab     string
		wantError string
	}{
		{
			name:      "lsblk fails on the requested disk",
			disks:     []ociDisk{ociBootDisk("sda"), {name: "sdb", tree: "sdb \n", lsblkFails: true}},
			requested: "sdb",
			wantError: "Cannot tell whether requested disk sdb is the boot volume",
		},
		{
			// mount_drives would format it, partition table and all.
			name: "requested disk has partitions",
			disks: []ociDisk{
				ociBootDisk("sda"),
				ociDataDisk("sdb"),
				{name: "sdc", tree: "sdc \nsdc1 \n"},
				ociDataDisk("sdd"),
			},
			requested: "sdb sdc",
			wantError: "Requested disk sdc has partitions or holders. Refusing to format it.",
		},
		{
			name: "requested disk holds another mount path's filesystem",
			disks: []ociDisk{
				ociBootDisk("sda"),
				{name: "sdb", tree: "sdb \n", uuid: "uuid-1"},
				{name: "sdc", tree: "sdc \n", uuid: "uuid-x"},
			},
			requested: "sdb sdc",
			fstab:     ociFstab("", "uuid-1"),
			wantError: "Requested disk sdb holds the filesystem",
		},
		{
			name: "multiple disks hold the same fstab UUID",
			disks: []ociDisk{
				ociBootDisk("sda"),
				{name: "sdb", tree: "sdb \n", uuid: "uuid-0"},
				{name: "sdc", tree: "sdc \n", uuid: "uuid-0"},
			},
			requested: "sdb sdc",
			fstab:     ociFstab("uuid-0", ""),
			wantError: "Multiple devices hold filesystem UUID uuid-0 for /mnt/d0",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			_, stdout, stderr, err := runOCIDiskResolver(
				t,
				test.disks,
				test.requested,
				"/mnt/d0 /mnt/d1",
				test.fstab,
			)
			if err == nil {
				t.Fatalf("resolved OCI drives, want an error\nstdout: %s", stdout)
			}
			if !strings.Contains(stderr, test.wantError) {
				t.Fatalf("stderr does not contain %q: %s", test.wantError, stderr)
			}
		})
	}
}

func TestRenderedScriptResolvesOCIDrives(t *testing.T) {
	modulePath := mountEphemeralDrivesModulePath(t)
	module := NewMountEphemeralDrive(filepath.Dir(modulePath))
	values := map[string]any{
		"cloud_type":     "oci",
		"device_paths":   "sdb",
		"mount_paths":    "/mnt/d0",
		"imdsv2required": "False",
	}

	rendered, err := module.RenderTemplates(context.Background(), values)
	if err != nil {
		t.Fatalf("render mount script: %v", err)
	}
	script := rendered.RenderedContent("run")
	for _, expected := range []string{
		`get_oci_drives() {`,
		`elif [[ $CLOUD_TYPE == "oci" ]]; then`,
		`get_ephemeral_drives_rv=$get_oci_drives_rv`,
		`if [[ $CLOUD_TYPE == "azu" || $CLOUD_TYPE == "oci" ]]; then`,
	} {
		if !strings.Contains(script, expected) {
			t.Fatalf("rendered script does not contain %q", expected)
		}
	}

	// Skipping a device mounted outside YBA must extend the probe loop only where the next
	// iteration probes another device, or the loop probes the same device forever.
	extension := "max_iterations=$((max_iterations+1))"
	guard := `if [[ $CLOUD_TYPE == "aws" && "$DRIVE_SCHEME" == "nvme" ]]; then`
	if count := strings.Count(script, extension); count != 1 {
		t.Fatalf("rendered script extends the probe loop %d times, want 1", count)
	}
	extensionOffset := strings.Index(script, extension)
	guardOffset := strings.LastIndex(script[:extensionOffset], guard)
	if guardOffset < 0 ||
		strings.TrimSpace(script[guardOffset+len(guard):extensionOffset]) != "" {
		t.Fatal("rendered script extends the probe loop outside the AWS NVMe path")
	}

	scriptPath := filepath.Join(t.TempDir(), "mount_ephemeral_drives.sh")
	if err := os.WriteFile(scriptPath, []byte(script), 0o700); err != nil {
		t.Fatalf("write rendered script: %v", err)
	}
	if output, err := exec.Command("bash", "-n", scriptPath).CombinedOutput(); err != nil {
		t.Fatalf("rendered script has invalid shell syntax: %v\n%s", err, output)
	}
}
