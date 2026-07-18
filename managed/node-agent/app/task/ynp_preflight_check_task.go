// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"os/exec"
)

// YnpreflightCheckHandler implements task.AsyncTask.
type YnpPreflightCheckHandler struct {
	shellTask *ShellTask
	param     *pb.YnpPreflightCheckInput
	logOut    util.Buffer
}

// NewYnpPreflightCheckHandler returns a new instance of YnpPreflightCheckHandler.
func NewYnpPreflightCheckHandler(param *pb.YnpPreflightCheckInput) *YnpPreflightCheckHandler {
	handler := &YnpPreflightCheckHandler{
		param:  param,
		logOut: util.NewBuffer(module.MaxBufferCapacity),
	}
	return handler
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *YnpPreflightCheckHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *YnpPreflightCheckHandler) String() string {
	return "YNP Preflight Check Task"
}

// Handle implements the AsyncTask method which performs the YNP preflight checks.
func (h *YnpPreflightCheckHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Debug(ctx, "Starting YNP Preflight checks handler.")
	configJson := h.param.GetConfigJson()
	if configJson == "" {
		err := fmt.Errorf("Input config JSON is empty")
		util.FileLogger().Errorf(ctx, "Error in YNP Preflight checks - %s", err.Error())
		return nil, err
	}
	tmpDir := h.param.GetRemoteTmp()
	if tmpDir == "" {
		err := fmt.Errorf("Remote tmp directory is not provided")
		util.FileLogger().Errorf(ctx, "Error in YNP Preflight checks - %s", err.Error())
		return nil, err
	}
	// Write the config JSON file to a temp file.
	configFile, err := os.CreateTemp(tmpDir, "ynp_config_*.json")
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in creating temp config file - %s", err.Error())
		return nil, err
	}
	defer os.Remove(configFile.Name())
	defer configFile.Close()
	preflightCheckOutFile, err := os.CreateTemp(tmpDir, "ynp_preflight_check_output_*.log")
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Error in creating temp preflight check output file - %s", err.Error())
		return nil, err
	}
	defer os.Remove(preflightCheckOutFile.Name())
	defer preflightCheckOutFile.Close()
	// Write the config JSON to the temp file.
	_, err = configFile.WriteString(configJson)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in writing to temp config file - %s", err.Error())
		return nil, err
	}
	util.FileLogger().Infof(ctx, "Using config JSON - %s", configJson)
	h.logOut.WriteLine("Using config JSON - %s", configJson)
	provisioner := util.NodeAgentProvisioner()
	cmd := fmt.Sprintf(
		"%s --preflight_check --config_file %s --preflight_check_out_file %s --extra_vars %s --cloud_type onprem",
		provisioner,
		util.NodeAgentProvisionYaml(),
		preflightCheckOutFile.Name(),
		configFile.Name(),
	)
	// Run the YNP preflight check command and capture the output and exit code.
	cInfo, err := module.RunShellCmd(ctx, "", "YNP Preflight Check", cmd, h.logOut)
	h.logOut.Write([]byte(cInfo.StdOut.String()))
	if err == nil {
		return &pb.DescribeTaskResponse{
			Data: &pb.DescribeTaskResponse_YnpPreflightCheckOutput{
				YnpPreflightCheckOutput: &pb.YnpPreflightCheckOutput{
					ExitCode: 0,
				},
			}}, nil
	} else {
		util.FileLogger().Errorf(ctx, "Error in YNP Preflight checks - %s", err.Error())
	}
	h.logOut.Write([]byte(cInfo.StdErr.String()))
	exitCode := 1
	if exitErr, ok := err.(*exec.ExitError); ok {
		exitCode = exitErr.ExitCode()
	}
	output, err := os.ReadFile(preflightCheckOutFile.Name())
	if err != nil {
		// Log as warning and ignore as it has already captured the error in stderr and exit code.
		util.FileLogger().
			Warnf(ctx, "Error in reading preflight check output file - %s", err.Error())
		output = []byte{}
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_YnpPreflightCheckOutput{
			YnpPreflightCheckOutput: &pb.YnpPreflightCheckOutput{
				ExitCode: uint32(exitCode),
				Output:   string(output),
			},
		}}, nil
}
