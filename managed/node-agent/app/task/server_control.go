// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strconv"
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
	if handler.param.GetNumVolumes() > 0 {
		cmd := "df | awk '{{print $6}}' | egrep '^/mnt/d[0-9]+' | wc -l"
		util.FileLogger().Infof(ctx, "Running command %v", cmd)
		cmdInfo, err := module.RunShellCmd(
			ctx,
			handler.username,
			"getNumVolumes",
			cmd,
			handler.logOut,
		)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Server control failed in %v - %s", cmd, err.Error())
			return nil, err
		}
		count, err := strconv.Atoi(strings.TrimSpace(cmdInfo.StdOut.String()))
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Failed to parse output of command %v - %s", cmd, err.Error())
			return nil, err
		}
		if uint32(count) < handler.param.GetNumVolumes() {
			err = fmt.Errorf(
				"Not all data volumes attached: needed %d found %d",
				handler.param.GetNumVolumes(),
				count,
			)
			util.FileLogger().Errorf(ctx, "Volume mount validation failed - %s", err.Error())
			return nil, err
		}
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
