// Copyright (c) YugaByte, Inc.

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

type InstallSoftwareHandler struct {
	shellTask *ShellTask
	param     *pb.InstallSoftwareInput
	username  string
	result    string
	logOut    util.Buffer
}

func NewInstallSoftwareHandler(
	param *pb.InstallSoftwareInput,
	username string,
) *InstallSoftwareHandler {
	return &InstallSoftwareHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *InstallSoftwareHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *InstallSoftwareHandler) String() string {
	return "Install Software Task"
}

// helper that wraps NewShellTaskWithUser + Process + error logging
func (h *InstallSoftwareHandler) runShell(
	ctx context.Context,
	desc, shell string,
	args []string,
) error {
	h.logOut.WriteLine("Running install software phase: %s", desc)
	h.shellTask = NewShellTaskWithUser(desc, h.username, shell, args)
	_, err := h.shellTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx,
			"Install software failed [%s]: %s", desc, err)
		return err
	}
	return nil
}

func (h *InstallSoftwareHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting install software handler.")

	ybPkg := h.param.GetYbPackage()
	if ybPkg == "" {
		err := errors.New("ybPackage is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 1) extract all the names and paths up front
	pkgName := filepath.Base(ybPkg)
	pkgFolder := helpers.ExtractArchiveFolderName(pkgName)
	_, err := helpers.ExtractYugabyteReleaseFolder(pkgName)
	if err != nil {
		return nil, err
	}
	releaseVersion, err := helpers.ExtractReleaseVersion(pkgName)
	if err != nil {
		return nil, err
	}

	// 2) download (if remote)
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
		if err := h.runShell(ctx, "download-software", util.DefaultShell, []string{"-c", cmdStr}); err != nil {
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

	// 3) figure out home dir
	home := ""
	if h.param.GetYbHomeDir() != "" {
		home = h.param.GetYbHomeDir()
	} else {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	ybSoftwareDir := filepath.Join(home, "yb-software", pkgFolder)

	// 4) define our sequence of shell steps
	err = h.execShellCommands(ctx, home, ybSoftwareDir, releaseVersion, tmpDir)
	if err != nil {
		return nil, err
	}

	// 5) symlink for master & tserver
	err = h.setupSymlinks(ctx, home, ybSoftwareDir)
	if err != nil {
		return nil, err
	}

	h.result = "Completed"
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_InstallSoftwareOutput{
			InstallSoftwareOutput: &pb.InstallSoftwareOutput{},
		},
	}, nil
}

func (h *InstallSoftwareHandler) execShellCommands(
	ctx context.Context,
	home string,
	ybSoftwareDir string,
	releaseVersion string,
	tmpDir string,
) error {
	releasesDir := filepath.Join(home, "releases", releaseVersion)
	steps := []struct {
		desc string
		cmd  string
	}{
		{"make-yb-software-dir", fmt.Sprintf("mkdir -p %s", ybSoftwareDir)},
		{
			"untar-software",
			fmt.Sprintf("tar -xzvf %s --strip-components=1 -C %s", tmpDir, ybSoftwareDir),
		},
		{"make-release-dir", fmt.Sprintf("mkdir -p %s", releasesDir)},
		{"copy-package-to-release", fmt.Sprintf("cp %s %s", tmpDir, releasesDir)},
		{"remove-temp-package", fmt.Sprintf("rm -rf %s", tmpDir)},
		{"post-install", filepath.Join(ybSoftwareDir, "bin/post_install.sh")},
		{
			"remove-older-release",
			fmt.Sprintf(
				`ls "%s/releases" -t | tail -n +%d | xargs -I{} --no-run-if-empty rm -rv "%s/releases/{}" --`,
				home,
				1,
				home,
			),
		},
	}

	for _, step := range steps {
		if err := h.runShell(ctx, step.desc, util.DefaultShell, []string{"-c", step.cmd}); err != nil {
			return err
		}
	}
	return nil
}

func (h *InstallSoftwareHandler) setupSymlinks(
	ctx context.Context,
	home string,
	ybSoftwareDir string,
) error {
	processes := []string{"master", "tserver"}
	files, err := helpers.ListDirectoryContent(ybSoftwareDir)
	if err != nil {
		return err
	}
	for _, proc := range processes {
		targetDir := filepath.Join(home, proc)
		for _, f := range files {
			src := filepath.Join(ybSoftwareDir, f)
			dst := filepath.Join(targetDir, f)
			desc := fmt.Sprintf("symlink-%s-to-%s", src, dst)
			cmd := fmt.Sprintf("ln -sf %s %s", src, dst)
			if err := h.runShell(ctx, desc, util.DefaultShell, []string{"-c", cmd}); err != nil {
				return err
			}
		}
	}
	return nil
}
