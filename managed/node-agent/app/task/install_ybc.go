// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task/helpers"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"strings"
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
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
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
		Desc string
		Cmd  string
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
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}

func (h *InstallYbcHandler) execConfigureYBCCommands(
	ctx context.Context,
	ybcSoftwareDir, ybcControllerDir string,
) error {
	mountPoints := h.param.GetMountPoints()
	if len(mountPoints) == 0 {
		return errors.New("mountPoints is required")
	}
	mountPoint := mountPoints[0]
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{
			"setup-bin-symlink",
			fmt.Sprintf(
				"rm -rf %s && ln -sf %s %s",
				filepath.Join(ybcControllerDir, "bin"),
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
				"rm -rf %s && ln -sf %s %s",
				filepath.Join(ybcControllerDir, "logs"),
				filepath.Join(mountPoint, "ybc-data/controller/logs"),
				filepath.Join(ybcControllerDir, "logs"),
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

	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
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
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	ybcSoftwareDir := filepath.Join(h.param.GetYbHomeDir(), "yb-software", pkgFolder)
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
	systemdCtx := map[string]any{
		"mount_paths":              strings.Join(h.param.GetMountPoints(), " "),
		"user_name":                h.username,
		"yb_home_dir":              h.param.GetYbHomeDir(),
		"use_system_level_systemd": false,
	}
	err = module.UpdateUserSystemdUnits(ctx, h.username, "controller", systemdCtx, h.logOut)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	return nil, nil
}
