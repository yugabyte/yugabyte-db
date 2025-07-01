// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
)

var registeredServices = []struct {
	unitName string
	home     string
}{
	{"yb-master.service", "master"},
	{"yb-tserver.service", "tserver"},
	{"yb-controller.service", "controller"},
	{"yb-clean_cores.service", ""},
	{"yb-zip_purge_yb_logs.timer", ""},
	{"yb-zip_purge_yb_logs.service", ""},
	{"yb-bind_check.service", ""},
	{"yb-collect_metrics.timer", ""},
	{"yb-collect_metrics.service", ""},
	{"otel-collector.service", ""},
}

type DestroyServerHandler struct {
	param    *pb.DestroyServerInput
	username string
	logOut   util.Buffer
}

// NewDestroyServerHandler returns a new instance of DestroyServerHandler.
func NewDestroyServerHandler(param *pb.DestroyServerInput, username string) *DestroyServerHandler {
	return &DestroyServerHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *DestroyServerHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

// String implements the AsyncTask method.
func (h *DestroyServerHandler) String() string {
	return "runDestroyServer"
}

func (h *DestroyServerHandler) cleanupNode(ctx context.Context) error {
	for _, info := range registeredServices {
		if info.home != "" {
			util.FileLogger().
				Infof(ctx, "Running control script to clean and clean-logs for %s", info.unitName)
			for _, action := range []string{"clean", "clean-logs"} {
				cmd := fmt.Sprintf("yb-server-ctl.sh %s %s", info.home, action)
				h.logOut.WriteLine("Running cleanup command %s", cmd)
				util.FileLogger().Infof(ctx, "Running command: %s", cmd)
				if _, err := module.RunShellCmd(ctx, h.username, action, cmd, h.logOut); err != nil {
					util.FileLogger().
						Errorf(ctx, "Failed to run %s command for %s - %s", cmd, info.unitName, err.Error())
					return err
				}
			}
		}
	}
	h.logOut.WriteLine("Running control script to clean instance")
	util.FileLogger().Info(ctx, "Running control script to clean instance")
	if _, err := module.RunShellCmd(
		ctx,
		h.username,
		"clean-instance",
		"yb-server-ctl.sh clean-instance",
		h.logOut,
	); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run clean-instance - %s", err.Error())
		return err
	}
	return nil
}

// Handle implements the AsyncTask method.
func (h *DestroyServerHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	for _, info := range registeredServices {
		unitPath, err := module.SystemdUnitPath(ctx, h.username, info.unitName, h.logOut)
		if err != nil {
			return nil, err
		}
		unitPathToBeRemoved := ""
		if h.param.GetIsProvisioningCleanup() {
			unitPathToBeRemoved = unitPath
		}
		h.logOut.WriteLine("Found systemd unit path for %s: %s", info.unitName, unitPath)
		if strings.HasPrefix(unitPath, "/") {
			if err := module.DisableSystemdService(ctx, h.username, info.unitName, unitPathToBeRemoved, h.logOut); err != nil {
				return nil, err
			}
			if info.home != "" {
				confFilepath := filepath.Join(h.param.GetYbHomeDir(), info.home, "conf/server.conf")
				util.FileLogger().Infof(ctx, "Removing server conf file %s", confFilepath)
				err = os.Remove(confFilepath)
				if err != nil && !os.IsNotExist(err) {
					util.FileLogger().
						Errorf(ctx, "Failed to delete conf file %s - %s", confFilepath, err.Error())
					return nil, err
				}
			}
		}
	}
	if err := h.cleanupNode(ctx); err != nil {
		return nil, err
	}
	return nil, nil
}
