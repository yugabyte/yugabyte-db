// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"fmt"
	"io/fs"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
)

type ServerControlHandler struct {
	param    *pb.ServerControlInput
	username string
	logOut   util.Buffer
}

// NewServerControlHandler returns a new instance of ServerControlHandler.
func NewServerControlHandler(param *pb.ServerControlInput, username string) *ServerControlHandler {
	return &ServerControlHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (handler *ServerControlHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       handler.logOut,
		ExitStatus: &ExitStatus{},
	}
}

// String implements the AsyncTask method.
func (handler *ServerControlHandler) String() string {
	return "runServerControl"
}

// Handle implements the AsyncTask method.
func (handler *ServerControlHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	if err := handler.checkDataVolumes(ctx); err != nil {
		return nil, err
	}
	// Enable linger for user level systemd.
	yes, _, err := module.IsUserSystemd(handler.username, handler.param.GetServerName())
	if err != nil {
		return nil, err
	}
	if yes {
		lingerCmd := fmt.Sprintf("loginctl enable-linger %s", handler.username)
		_, err := module.RunShellCmd(
			ctx,
			handler.username,
			"loginctl enable-linger",
			lingerCmd,
			handler.logOut,
		)
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Server control failed in %v - %s", lingerCmd, err.Error())
			return nil, err
		}
	}
	controlType := strings.ToLower(pb.ServerControlType_name[int32(handler.param.ControlType)])
	err = module.ControlSystemdService(
		ctx,
		handler.username,
		handler.param.GetServerName(),
		controlType,
		handler.logOut,
	)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Server control failed for %s - %s", handler.param.GetServerName(), err.Error())
		return nil, err
	}
	if handler.param.GetDeconfigure() {
		confFilepath := filepath.Join(handler.param.GetServerHome(), "conf", "server.conf")
		util.FileLogger().Infof(ctx, "Removing server conf file %s", confFilepath)
		err = os.Remove(confFilepath)
		if err != nil && !os.IsNotExist(err) {
			return nil, err
		}
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_ServerControlOutput{
			// TODO set pid.
			ServerControlOutput: &pb.ServerControlOutput{},
		},
	}, nil
}

// checkDataVolumes checks if the data volumes are attached.
func (handler *ServerControlHandler) checkDataVolumes(ctx context.Context) error {
	if !handler.param.GetCheckDataVolumes() {
		return nil
	}
	mountPaths := handler.param.GetMountPoints()
	if len(mountPaths) == 0 {
		return nil
	}
	ybHome := filepath.Dir(handler.param.GetServerHome())
	binDir := filepath.Join(ybHome, "bin")
	diskCheckPath := filepath.Join(binDir, "disk-check.sh")
	if err := os.MkdirAll(binDir, 0o755); err != nil {
		return err
	}
	// Refresh the script as there can be new changes.
	if _, err := module.CopyFile(
		ctx,
		map[string]any{
			"mount_paths":        strings.Join(mountPaths, " "),
			"check_data_volumes": true,
		},
		filepath.Join(module.ServerTemplateSubpath, "disk-check.sh.j2"),
		diskCheckPath,
		fs.FileMode(0755),
		handler.username,
	); err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to copy disk-check.sh - %s", err.Error())
		return err
	}
	// Script has retries built in, so we don't need to retry here.
	util.FileLogger().Infof(ctx, "Running disk checks: %v", diskCheckPath)
	_, err := module.RunShellCmd(
		ctx,
		handler.username,
		"disk-check",
		diskCheckPath,
		handler.logOut,
	)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Server control failed in %v - %s", diskCheckPath, err.Error())
		return err
	}
	return nil
}
