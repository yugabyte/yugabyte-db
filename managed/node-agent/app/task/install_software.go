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
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
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

func (h *InstallSoftwareHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting install software handler.")

	ybPkg := h.param.GetYbPackage()
	if ybPkg == "" {
		err := errors.New("ybPackage is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	if len(h.param.GetSymLinkFolders()) == 0 {
		err := errors.New("server process is required")
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

	// 2) figure out home dir
	home := ""
	if h.param.GetYbHomeDir() != "" {
		home = h.param.GetYbHomeDir()
	} else {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	ybSoftwareDir := filepath.Join(home, "yb-software", pkgFolder)

	// 3) symlink for master & tserver
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

func (h *InstallSoftwareHandler) setupSymlinks(
	ctx context.Context,
	home string,
	ybSoftwareDir string,
) error {
	processes := h.param.GetSymLinkFolders()
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
			cmd := fmt.Sprintf("rm -rf %s && ln -sf %s %s", dst, src, dst)
			if _, err := module.RunShellCmd(ctx, h.username, desc, cmd, h.logOut); err != nil {
				return err
			}
		}
	}
	return nil
}
