// Copyright (c) YugaByte, Inc.

package task

import (
	"bytes"
	"context"
	"io/fs"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"strconv"
	"strings"
	"sync/atomic"
)

const (
	ServerConfSubpath         = "conf/server.conf"
	ServerConfTemplateSubpath = "server/yb-server-gflags.j2"
)

var (
	MasterStateDirs = []string{
		"wals", "data", "consensus-meta", "instance", "auto_flags_config", "tablet-meta"}
)

type ServerGflagsHandler struct {
	taskStatus *atomic.Value
	param      *pb.ServerGFlagsInput
	username   string
	logOut     util.Buffer
}

// NewServerGflagsHandler returns a new instance of ServerControlHandler.
func NewServerGflagsHandler(param *pb.ServerGFlagsInput, username string) *ServerGflagsHandler {
	return &ServerGflagsHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (handler *ServerGflagsHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       handler.logOut,
		ExitStatus: &ExitStatus{},
	}
}

// String implements the AsyncTask method.
func (handler *ServerGflagsHandler) String() string {
	return "runServerGflags"
}

func (handler *ServerGflagsHandler) postmasterCgroupPath(ctx context.Context) (string, error) {
	userInfo, err := util.UserInfo(handler.username)
	if err != nil {
		return "", err
	}
	handler.logOut.WriteLine("Determining cgroup version")
	cmd, err := module.NewCommandWithUser(
		"DetermineCgroupVersion",
		handler.username,
		"stat",
		[]string{"-fc", "%%T", "/sys/fs/cgroup/"},
	).Create(ctx)
	if err != nil {
		return "", err
	}
	buffer := &bytes.Buffer{}
	cmd.Stdout = buffer
	err = cmd.Run()
	if err != nil {
		return "", err
	}
	userID := strconv.Itoa(int(userInfo.UserID))
	postmasterCgroupPath := "/sys/fs/cgroup/memory/ysql"
	stdout := strings.TrimSpace(buffer.String())
	if stdout == "cgroup2fs" {
		postmasterCgroupPath = filepath.Join(
			"/sys/fs/cgroup/user.slice/user-",
			userID,
			".slice/user@",
			userID,
			".service/ysql")
	}
	return postmasterCgroupPath, nil
}

func (handler *ServerGflagsHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	gflags := handler.param.GetGflags()
	if handler.param.GetResetMasterState() {
		if fsDataDirsCsv, ok := gflags["fs_data_dirs"]; ok {
			util.FileLogger().
				Infof(ctx, "Deleting master state dirs in fs_data_dirs: %s", fsDataDirsCsv)
			handler.logOut.WriteLine("Deleting master state dirs in fs_data_dirs: %s",
				fsDataDirsCsv)
			fsDataDirs := strings.Split(fsDataDirsCsv, ",")
			toDeletePaths := []string{}
			for _, fsDataDir := range fsDataDirs {
				for _, stateDir := range MasterStateDirs {
					// Example path is /mnt/d0/yb-data/master/wals.
					path := filepath.Join(fsDataDir, "yb-data", "master", stateDir)
					toDeletePaths = append(toDeletePaths, path)
				}
			}
			if len(toDeletePaths) > 0 {
				command := module.NewCommandWithUser(
					"DeleteMasterState",
					handler.username,
					"rm",
					[]string{"-rf", strings.Join(toDeletePaths, " ")},
				)
				cmd, err := command.Create(ctx)
				if err == nil {
					err = cmd.Run()
				}
				if err != nil {
					util.FileLogger().
						Errorf(ctx, "Failed to delete master paths %v: %v", toDeletePaths, err)
					return nil, err
				}
			}
		}
	}
	processedGflags := gflags
	if _, yes := gflags["postmaster_cgroup"]; yes {
		path, err := handler.postmasterCgroupPath(ctx)
		if err != nil {
			return nil, err
		}
		processedGflags := map[string]any{}
		for k, v := range gflags {
			if k == "postmaster_cgroup" {
				processedGflags["postmaster_cgroup"] = path
			} else {
				processedGflags[k] = v
			}
		}
	}
	gflagsContext := map[string]any{
		"gflags": processedGflags,
	}
	destination := filepath.Join(handler.param.GetServerHome(), ServerConfSubpath)
	err := module.CopyFile(
		ctx,
		gflagsContext,
		ServerConfTemplateSubpath,
		destination,
		fs.FileMode(0755),
	)
	if err != nil {
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_ServerGFlagsOutput{
			// TODO set pid.
			ServerGFlagsOutput: &pb.ServerGFlagsOutput{},
		},
	}, nil
}
