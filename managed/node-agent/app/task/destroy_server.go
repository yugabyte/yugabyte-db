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
	unitName          string
	home              string
	needsProvisioning bool
}{
	{"yb-master.service", "master", false},
	{"yb-tserver.service", "tserver", false},
	{"yb-controller.service", "controller", false},
	{"yb-clean_cores.service", "", false},
	{"yb-clean_cores.timer", "", false},
	{"yb-zip_purge_yb_logs.service", "", false},
	{"yb-zip_purge_yb_logs.timer", "", false},
	{"yb-bind_check.service", "", false},
	{"yb-collect_metrics.service", "", false},
	{"yb-collect_metrics.timer", "", false},
	{"otel-collector.service", "", false},
	{"node_exporter.service", "", true},
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
	cleanupScript := filepath.Join(h.param.GetYbHomeDir(), "bin/yb-server-ctl.sh")
	if _, err := os.Stat(cleanupScript); err != nil && os.IsNotExist(err) {
		h.logOut.WriteLine("Skipping node cleanup as file %s is not found", cleanupScript)
		util.FileLogger().Warnf(ctx, "Skipping node cleanup as file %s is not found", cleanupScript)
		return nil
	}
	for _, info := range registeredServices {
		if info.home != "" {
			util.FileLogger().
				Infof(ctx, "Running control script to clean and clean-logs for %s", info.unitName)
			for _, action := range []string{"clean", "clean-logs"} {
				cmd := fmt.Sprintf("%s %s %s", cleanupScript, info.home, action)
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
		fmt.Sprintf("%s clean-instance", cleanupScript),
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
		if info.needsProvisioning && !h.param.GetIsProvisioningCleanup() {
			h.logOut.WriteLine(
				"Skipping %s as it is not part of provisioning cleanup",
				info.unitName,
			)
			continue
		}
		unitPath, err := module.SystemdUnitPath(ctx, h.username, info.unitName, h.logOut)
		if err != nil {
			return nil, err
		}
		h.logOut.WriteLine("Found systemd unit path for %s: %s", info.unitName, unitPath)
		if strings.HasPrefix(unitPath, "/") {
			unitPathToBeRemoved := ""
			if h.param.GetIsProvisioningCleanup() {
				unitPathToBeRemoved = unitPath
			}
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
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_DestroyServerOutput{
			DestroyServerOutput: &pb.DestroyServerOutput{},
		},
	}, nil
}
