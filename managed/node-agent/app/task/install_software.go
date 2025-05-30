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
	releaseVersion, err := helpers.ExtractReleaseVersion(pkgName)
	if err != nil {
		return nil, err
	}
	tmpDir := filepath.Join(h.param.GetRemoteTmp(), pkgName)

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

	// 3) define our sequence of shell steps
	err = h.execShellCommands(ctx, home, ybSoftwareDir, releaseVersion, tmpDir)
	if err != nil {
		return nil, err
	}

	// 4) symlink for master & tserver
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
		Desc string
		Cmd  string
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
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
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
			cmd := fmt.Sprintf("unlink %s > /dev/null 2>&1; ln -sf %s %s", dst, src, dst)
			if _, err := module.RunShellCmd(ctx, h.username, desc, cmd, h.logOut); err != nil {
				return err
			}
		}
	}
	return nil
}
