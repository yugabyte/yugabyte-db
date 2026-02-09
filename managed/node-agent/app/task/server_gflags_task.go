// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"strconv"
	"strings"
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
	param    *pb.ServerGFlagsInput
	username string
	logOut   util.Buffer
}

// NewServerGflagsHandler returns a new instance of ServerControlHandler.
func NewServerGflagsHandler(param *pb.ServerGFlagsInput, username string) *ServerGflagsHandler {
	return &ServerGflagsHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
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
	cmdInfo := &module.CommandInfo{
		User:   handler.username,
		Desc:   "DetermineCgroupVersion",
		Cmd:    "stat",
		Args:   []string{"-fc", "%T", "/sys/fs/cgroup/"},
		StdOut: util.NewBuffer(module.MaxBufferCapacity),
	}
	err = cmdInfo.RunCmd(ctx)
	if err != nil {
		return "", err
	}
	userID := strconv.Itoa(int(userInfo.UserID))
	stdout := strings.TrimSpace(cmdInfo.StdOut.String())
	return buildPostmasterCgroupPath(stdout, userID), nil
}

// buildPostmasterCgroupPath returns the postmaster cgroup path for the given cgroup stat output and user ID.
// cgroupStatOutput is the trimmed output of: stat -fc %T /sys/fs/cgroup/ (e.g. "cgroup2fs" or "tmpfs").
func buildPostmasterCgroupPath(cgroupStatOutput string, userID string) string {
	if cgroupStatOutput != "cgroup2fs" {
		return "/sys/fs/cgroup/memory/ysql"
	}
	return filepath.Join("/sys/fs/cgroup",
		"user.slice",
		fmt.Sprintf("user-%s.slice", userID),
		fmt.Sprintf("user@%s.service", userID),
		"ysql")
}

func (handler *ServerGflagsHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	gflags := handler.param.GetGflags()
	if handler.param.GetResetMasterState() {
		// Verify that master process is not running.
		running, err := module.IsProcessRunning(ctx, handler.username, "yb-master", handler.logOut)
		if err != nil {
			return nil, err
		}
		if running {
			util.FileLogger().
				Infof(ctx, "Master process must be stopped before resetting state")
			return nil, errors.New("Master process must be stopped before resetting state")
		}
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
				rmArgs := make([]string, 0, len(toDeletePaths)+1)
				rmArgs = append(rmArgs, "-rf")
				rmArgs = append(rmArgs, toDeletePaths...)
				cmdInfo := &module.CommandInfo{
					User: handler.username,
					Desc: "DeleteMasterState",
					Cmd:  "rm",
					Args: rmArgs,
				}
				err := cmdInfo.RunCmd(ctx)
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
		processedGflags = map[string]string{}
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
		fs.FileMode(0644),
		handler.username,
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
