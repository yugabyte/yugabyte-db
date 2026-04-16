// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"path/filepath"

	"node-agent/app/task/helpers"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
)

type DownloadSoftwareHandler struct {
	param    *pb.DownloadSoftwareInput
	username string
	result   string
	logOut   util.Buffer
}

func NewDownloadSoftwareHandler(
	param *pb.DownloadSoftwareInput,
	username string,
) *DownloadSoftwareHandler {
	return &DownloadSoftwareHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *DownloadSoftwareHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *DownloadSoftwareHandler) String() string {
	return "Download Software Task"
}

func (h *DownloadSoftwareHandler) execShellCommands(
	ctx context.Context,
	home string,
	ybSoftwareDir string,
	releaseVersion string,
	tmpDir string,
	pkgName string,
	pkgFolder string,
	numReleasesToKeep uint32,
) error {
	releasesDir := filepath.Join(home, "releases", releaseVersion)
	temporaryPackageDir := filepath.Join(ybSoftwareDir, "TEMPORARY")
	ybSoftwarePkgDir := filepath.Join(ybSoftwareDir, pkgFolder)
	tempPackage := filepath.Join(temporaryPackageDir, pkgName)
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{"clean-yb-software-dir", fmt.Sprintf("rm -rf %s", ybSoftwarePkgDir)},
		{"make-yb-software-dir", fmt.Sprintf("mkdir -m 755 -p %s", ybSoftwarePkgDir)},
		{
			"make-yb-software-temporary-dir",
			fmt.Sprintf(
				"rm -rf %s && mkdir -m 755 -p %s",
				temporaryPackageDir,
				temporaryPackageDir,
			),
		},
		{"move-yb-software-to-TEMPORARAY", fmt.Sprintf("mv %s %s", tmpDir, temporaryPackageDir)},
		{
			"untar-software",
			fmt.Sprintf("tar -xzvf %s --strip-components=1 -C %s", tempPackage, ybSoftwarePkgDir),
		},
		{"make-release-dir", fmt.Sprintf("mkdir -m 755 -p %s", releasesDir)},
		{"copy-package-to-release", fmt.Sprintf("mv -f %s %s", tempPackage, releasesDir)},
		{"post-install", filepath.Join(ybSoftwarePkgDir, "bin/post_install.sh")},
		{
			"remove-older-release",
			fmt.Sprintf(
				`ls --ignore='ybc*' %s -t | tail -n +%d | xargs -I{} --no-run-if-empty rm -rv "%s/{}" --`,
				ybSoftwareDir,
				numReleasesToKeep+3,
				ybSoftwareDir,
			),
		},
	}
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}

func (h *DownloadSoftwareHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting download software handler.")

	ybPkg := h.param.GetYbPackage()
	if ybPkg == "" {
		err := errors.New("ybPackage is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 1) extract all the names and paths up front
	pkgName := filepath.Base(ybPkg)
	pkgFolder := helpers.ExtractArchiveFolderName(pkgName)
	releaseVersion, err := helpers.ExtractReleaseVersion(pkgName)
	if err != nil {
		return nil, err
	}

	// 2) figure out home dir
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	ybSoftwareDir := filepath.Join(h.param.GetYbHomeDir(), "yb-software")

	// 3) download (if remote)
	tmpDir := filepath.Join(h.param.GetRemoteTmp(), pkgName)
	cmdStr, err := module.DownloadSoftwareCommand(h.param, tmpDir)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Download cmd generation failed: %s", err)
		return nil, err
	}
	util.FileLogger().Infof(ctx, "Download command %s", cmdStr)
	h.logOut.WriteLine("Download command %s", cmdStr)
	if cmdStr != "" {
		h.logOut.WriteLine("Dowloading software")
		if _, err := module.RunShellCmd(ctx, h.username, "download-software", cmdStr, h.logOut); err != nil {
			return nil, err
		}
		// optional checksum
		if h.param.GetHttpRemoteDownload() && h.param.GetHttpPackageChecksum() != "" {
			if err := module.VerifyChecksum(tmpDir, h.param.GetHttpPackageChecksum()); err != nil {
				return nil, err
			}
		}
		util.FileLogger().Debugf(ctx, "Successfully downloaded the software")
	}

	// 4) define our sequence of shell steps
	err = h.execShellCommands(
		ctx,
		h.param.GetYbHomeDir(),
		ybSoftwareDir,
		releaseVersion,
		tmpDir,
		pkgName,
		pkgFolder,
		h.param.GetNumReleasesToKeep(),
	)
	if err != nil {
		return nil, err
	}

	h.result = "Completed"
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_DownloadSoftwareOutput{
			DownloadSoftwareOutput: &pb.DownloadSoftwareOutput{},
		},
	}, nil
}
