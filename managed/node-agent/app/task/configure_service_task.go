// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"fmt"
	pb "node-agent/generated/service"
	"node-agent/util"
	"strings"
)

// PreflightCheckHandler implements task.AsyncTask.
type ConfigureServiceHandler struct {
	shellTask      *ShellTask
	param          *pb.ServiceConfig
	serviceEnabled bool
	result         string
}

// NewConfigureServiceHandler returns a new instance of ConfigureServiceHandler.
func NewConfigureServiceHandler(
	param *pb.ServiceConfig,
	enabled bool,
) *ConfigureServiceHandler {

	handler := &ConfigureServiceHandler{
		param:          param,
		serviceEnabled: enabled,
		shellTask:      nil,
	}
	return handler
}

// CurrentTaskStatus implements the AsyncTask method.
func (handler *ConfigureServiceHandler) CurrentTaskStatus() *TaskStatus {
	// We init eventual task only after we check provisioning status.
	if handler.shellTask == nil {
		return &TaskStatus{
			Info: util.NewBuffer(1),
		}
	}
	taskStatus := handler.shellTask.CurrentTaskStatus()
	taskStatus.Info = util.NewBuffer(1)
	return taskStatus
}

// String implements the AsyncTask method.
func (handler *ConfigureServiceHandler) String() string {
	return handler.shellTask.String()
}

func (handler *ConfigureServiceHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	ybHomeDir := handler.param.GetYbHomeDir()
	util.FileLogger().Debug(ctx, "Starting install/configure earlyoom handler.")
	checkProvisionedTask := NewShellTaskWithUser(
		"checkProvisioned",
		"yugabyte",
		util.DefaultShell,
		[]string{fmt.Sprintf("ls %s/bin/configure_earlyoom_service.sh", ybHomeDir)},
	)
	provisionedStatus, err := checkProvisionedTask.Process(ctx)
	earlyoomProvisioned := false
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to check if earlyoom is installed - %s", err)
	} else {
		earlyoomProvisioned = (provisionedStatus.ExitStatus.Code == 0)
	}

	user := "yugabyte"
	if !earlyoomProvisioned {
		user = ""
	}
	command, options := handler.getCommandWithOptions(ybHomeDir, earlyoomProvisioned)
	handler.shellTask = NewShellTaskWithUser(
		"runConfigureEarlyoomService",
		user,
		command,
		options,
	)
	argsList := strings.Join(handler.shellTask.args, " ")
	util.FileLogger().Infof(ctx, "Starting install/configure earlyoom handler with %s", argsList)

	output, err := handler.shellTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure earlyoom processing failed - %s", err.Error())
		return nil, err
	}
	data := output.Info.String()
	util.FileLogger().Debugf(ctx, "Configure earlyoom output data: %s", data)
	handler.result = "Completed"
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_ConfigureServiceOutput{
			ConfigureServiceOutput: &pb.ConfigureServiceOutput{},
		},
	}, nil
}

func (handler *ConfigureServiceHandler) Result() string {
	return handler.result
}

// Returns options for the preflight checks.
func (handler *ConfigureServiceHandler) getCommandWithOptions(
	ybHomeDir string,
	earlyoomProvisioned bool,
) (string, []string) {
	args := "\"" + handler.param.GetEarlyoomConfig().GetStartArgs() + "\""
	if earlyoomProvisioned {
		command := fmt.Sprintf(
			"%s %s/bin/configure_earlyoom_service.sh",
			util.DefaultShell,
			ybHomeDir,
		)
		options := []string{"-a"}
		if handler.serviceEnabled {
			options = append(options, "enable")
			if handler.param.GetEarlyoomConfig() != nil &&
				len(handler.param.GetEarlyoomConfig().GetStartArgs()) > 0 {
				options = append(options, "-c")
				options = append(options, args)
			}
		} else {
			options = append(options, "disable")
		}
		return command, options
	} else {
		return util.EarlyoomScriptPath(), []string{"--enable", "--earlyoom_args", args, "--yb_home_dir", ybHomeDir}
	}
}
