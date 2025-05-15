// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task/helpers"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
)

type InstallYbcHandler struct {
	shellTask *ShellTask
	param     *pb.InstallYbcInput
	username  string
	logOut    util.Buffer
}

func NewInstallYbcHandler(param *pb.InstallYbcInput, username string) *InstallYbcHandler {
	return &InstallYbcHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(MaxBufferCapacity),
	}
}

// helper that wraps NewShellTaskWithUser + Process + error logging
func (h *InstallYbcHandler) runShell(
	ctx context.Context,
	desc, shell string,
	args []string,
) error {
	h.logOut.WriteLine("Running install YBC phase: %s", desc)
	h.shellTask = NewShellTaskWithUser(desc, h.username, shell, args)
	_, err := h.shellTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx,
			"Install YBC failed [%s]: %s", desc, err)
		return err
	}
	return nil
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *InstallYbcHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *InstallYbcHandler) String() string {
	return "Install YBC Task"
}

func (h *InstallYbcHandler) execSetupYBCCommands(
	ctx context.Context,
	ybcPackagePath, ybcSoftwareDir, ybcControllerDir string,
) error {
	/*
		ybcPackagePath - Points to the current location where the YBC package is stored
		ybcSoftwareDir - Points to the location where YBC package will be stored.
		Example - /home/yugabyte/yb-software/ybc-2.2.0.2-b2-linux-x86_64
		ybcControllerDir - YBC directory on the node, /home/yugabyte/controller.
	*/

	steps := []struct {
		desc string
		cmd  string
	}{
		{"clean-ybc-software-dir", fmt.Sprintf("rm -rf %s", ybcSoftwareDir)},
		{
			"make-ybc-software-dir",
			fmt.Sprintf(
				"mkdir -p %s && chown %s:%s %s && chmod 0755 %s",
				ybcSoftwareDir,
				h.username,
				h.username,
				ybcSoftwareDir,
				ybcSoftwareDir,
			),
		},
		{
			"untar-ybc-software",
			fmt.Sprintf(
				"tar --no-same-owner -xzvf %s --strip-components=1 -C %s",
				ybcPackagePath,
				ybcSoftwareDir,
			),
		},
		{
			"make-controller-dir",
			fmt.Sprintf(
				"mkdir -p %s && chown %s:%s %s && chmod 0755 %s",
				ybcControllerDir,
				h.username,
				h.username,
				ybcControllerDir,
				ybcControllerDir,
			),
		},
		{"remove-temp-package", fmt.Sprintf("rm -rf %s", ybcPackagePath)},
	}

	for _, step := range steps {
		if err := h.runShell(ctx, step.desc, util.DefaultShell, []string{"-c", step.cmd}); err != nil {
			return err
		}
	}
	return nil
}

func (h *InstallYbcHandler) execConfigureYBCCommands(
	ctx context.Context,
	ybcSoftwareDir, ybcControllerDir string,
) error {
	mountPoint := ""
	if len(h.param.GetMountPoints()) > 0 {
		mountPoint = h.param.GetMountPoints()[0]
	}
	steps := []struct {
		desc string
		cmd  string
	}{
		{
			"setup-bin-symlink",
			fmt.Sprintf(
				"ln -sf %s %s",
				filepath.Join(ybcSoftwareDir, "bin"),
				filepath.Join(ybcControllerDir, "bin"),
			),
		},
		{
			"create-ybc-logs-dir-mount-path",
			fmt.Sprintf(
				"mkdir -p %s && chown %s:%s %s && chmod 0755 %s",
				filepath.Join(mountPoint, "ybc-data/controller/logs"),
				h.username,
				h.username,
				filepath.Join(mountPoint, "ybc-data/controller/logs"),
				filepath.Join(mountPoint, "ybc-data/controller/logs"),
			),
		},
		{
			"create-logs-dir-symlinks",
			fmt.Sprintf(
				"ln -sf %s %s",
				filepath.Join(mountPoint, "ybc-data/controller/logs"),
				filepath.Join(ybcControllerDir),
			),
		},
		{
			"create-ybc-conf-dir",
			fmt.Sprintf(
				"mkdir -p %s && chown %s:%s %s && chmod 0755 %s",
				filepath.Join(ybcControllerDir, "conf"),
				h.username,
				h.username,
				filepath.Join(ybcControllerDir, "conf"),
				filepath.Join(ybcControllerDir, "conf"),
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

func (h *InstallYbcHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting install YBC handler.")

	ybcPkg := h.param.GetYbcPackage()
	if ybcPkg == "" {
		err := errors.New("ybPackage is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 1) extract all the names and paths up front
	pkgName := filepath.Base(ybcPkg)
	pkgFolder := helpers.ExtractArchiveFolderName(pkgName)

	// 2) figure out home dir
	home := ""
	if h.param.GetYbHomeDir() != "" {
		home = h.param.GetYbHomeDir()
	} else {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	ybcSoftwareDir := filepath.Join(home, "yb-software", pkgFolder)
	ybcControllerDir := filepath.Join(h.param.GetYbHomeDir(), "controller")
	// 3) Put the ybc software at the desired location.
	ybcPackagePath := filepath.Join(h.param.GetRemoteTmp(), pkgName)
	err := h.execSetupYBCCommands(ctx, ybcPackagePath, ybcSoftwareDir, ybcControllerDir)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 4) Configure the ybc package.
	err = h.execConfigureYBCCommands(ctx, ybcSoftwareDir, ybcControllerDir)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	return nil, nil
}
