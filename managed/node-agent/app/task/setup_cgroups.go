// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/app/task/helpers"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"strconv"
	"strings"
)

const YsqlCgroupService = "yb-ysql-cgroup.service"

type SetupCgroupHandler struct {
	param    *pb.SetupCGroupInput
	username string
	logOut   util.Buffer
}

func NewSetupCgroupHandler(param *pb.SetupCGroupInput, username string) *SetupCgroupHandler {
	return &SetupCgroupHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *SetupCgroupHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *SetupCgroupHandler) String() string {
	return "Setup cGroup Task"
}

func (h *SetupCgroupHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting setup cGroup handler.")

	// 1) Retrieve OS information.
	osInfo, err := helpers.GetOSInfo()
	if err != nil {
		err := errors.New("error retrieving OS information")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 2) figure out home dir
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// Setup cGroups for rhel:9 deployments
	if helpers.IsRhel9(osInfo) {
		h.logOut.WriteLine("Determining cgroup version")
		cmdInfo := &module.CommandInfo{
			User:   h.username,
			Desc:   "DetermineCgroupVersion",
			Cmd:    "stat",
			Args:   []string{"-fc", "%T", "/sys/fs/cgroup/"},
			StdOut: util.NewBuffer(module.MaxBufferCapacity),
		}
		util.FileLogger().Infof(ctx, "Running command %v", cmdInfo)
		err = cmdInfo.RunCmd(ctx)
		if err != nil {
			return nil, err
		}

		userInfo, _ := util.UserInfo(h.username)
		stdout := strings.TrimSpace(cmdInfo.StdOut.String())
		userID := strconv.Itoa(int(userInfo.UserID))
		cGroupPath := "memory/ysql"
		memMax := "memory.limit_in_bytes"
		memSwapMap := "memory.memsw.limit_in_bytes"

		if stdout == "cgroup2fs" {
			cGroupPath = filepath.Join(
				fmt.Sprintf("user.slice/user-%s.slice", userID),
				fmt.Sprintf("user@%s.service", userID),
				"ysql")
			memMax = "memory.max"
			memSwapMap = "memory.swap.max"
		}

		cGroupServiceContext := map[string]any{
			"cgroup_path":   cGroupPath,
			"mem_max":       memMax,
			"mem_swap_max":  memSwapMap,
			"pg_max_mem_mb": h.param.GetPgMaxMemMb(),
		}

		h.logOut.WriteLine("Configuring cgroup systemd unit")
		// Copy yb-ysql-cgroup.service.
		_, err = module.CopyFile(
			ctx,
			cGroupServiceContext,
			filepath.Join(module.ServerTemplateSubpath, YsqlCgroupService),
			filepath.Join(h.param.GetYbHomeDir(), module.UserSystemdUnitPath, YsqlCgroupService),
			fs.FileMode(0755),
			h.username,
		)
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Server control failed for %s - %s", YsqlCgroupService, err.Error())
			return nil, err
		}

		err := module.ControlSystemdService(
			ctx,
			h.username,
			YsqlCgroupService,
			"start",
			h.logOut,
		)
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Server control failed for %s - %s", YsqlCgroupService, err.Error())
			return nil, err
		}
	}

	return nil, nil
}
