// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"errors"
	"path/filepath"

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

	h.result = "Completed"
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_DownloadSoftwareOutput{
			DownloadSoftwareOutput: &pb.DownloadSoftwareOutput{},
		},
	}, nil
}
