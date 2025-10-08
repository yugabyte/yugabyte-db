// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"encoding/json"
	"fmt"
	pb "node-agent/generated/service"
	"node-agent/model"
	"node-agent/util"
	"strings"
)

// PreflightCheckHandler implements task.AsyncTask.
type PreflightCheckHandler struct {
	shellTask *ShellTask
	param     *model.PreflightCheckParam
	result    *[]model.NodeConfig
}

// NewPreflightCheckHandler returns a new instance of PreflightCheckHandler.
func NewPreflightCheckHandler(param *model.PreflightCheckParam) *PreflightCheckHandler {
	handler := &PreflightCheckHandler{
		param: param,
	}
	handler.shellTask = NewShellTask(
		"runPreflightCheckScript",
		util.DefaultShell,
		handler.getOptions(util.PreflightCheckPath()),
	)
	return handler
}

// CurrentTaskStatus implements the AsyncTask method.
func (handler *PreflightCheckHandler) CurrentTaskStatus() *TaskStatus {
	taskStatus := handler.shellTask.CurrentTaskStatus()
	taskStatus.Info = util.NewBuffer(1)
	return taskStatus
}

// String implements the AsyncTask method.
func (handler *PreflightCheckHandler) String() string {
	return handler.shellTask.String()
}

func (handler *PreflightCheckHandler) Handle(
	ctx context.Context,
) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Debug(ctx, "Starting Preflight checks handler.")
	output, err := handler.shellTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Pre-flight checks processing failed - %s", err.Error())
		return nil, err
	}
	data := output.Info.String()
	util.FileLogger().Debugf(ctx, "Preflight check output data: %s", data)
	outputMap := map[string]model.PreflightCheckVal{}
	err = json.Unmarshal([]byte(data), &outputMap)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Pre-flight checks unmarshaling error - %s", err.Error())
		return nil, err
	}
	handler.result = getNodeConfig(outputMap)
	nodeConfigs := []*pb.NodeConfig{}
	err = util.ConvertType(handler.result, &nodeConfigs)
	if err != nil {
		return nil, err
	}
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_PreflightCheckOutput{
			PreflightCheckOutput: &pb.PreflightCheckOutput{
				NodeConfigs: nodeConfigs,
			},
		},
	}, nil
}

func (handler *PreflightCheckHandler) Result() *[]model.NodeConfig {
	return handler.result
}

// Returns options for the preflight checks.
func (handler *PreflightCheckHandler) getOptions(preflightScriptPath string) []string {
	options := []string{preflightScriptPath, "-t"}
	if handler.param.SkipProvisioning {
		options = append(options, "configure")
	} else {
		options = append(options, "provision")
	}
	options = append(options, "--node_agent_mode")
	options = append(options, "--yb_home_dir", "'"+handler.param.YbHomeDir+"'")
	if handler.param.SshPort != 0 {
		options = append(
			options,
			"--ssh_port",
			fmt.Sprint(handler.param.SshPort))
	}
	if handler.param.MasterHttpPort != 0 {
		options = append(
			options, "--master_http_port", fmt.Sprint(handler.param.MasterHttpPort))
	}
	if handler.param.MasterRpcPort != 0 {
		options = append(
			options, "--master_rpc_port", fmt.Sprint(handler.param.MasterRpcPort))
	}
	if handler.param.TserverHttpPort != 0 {
		options = append(
			options, "--tserver_http_port", fmt.Sprint(handler.param.TserverHttpPort))
	}
	if handler.param.TserverRpcPort != 0 {
		options = append(
			options, "--tserver_rpc_port", fmt.Sprint(handler.param.TserverRpcPort))
	}
	if handler.param.RedisServerHttpPort != 0 {
		options = append(
			options, "--redis_server_http_port", fmt.Sprint(handler.param.RedisServerHttpPort),
		)
	}
	if handler.param.RedisServerRpcPort != 0 {
		options = append(
			options, "--redis_server_rpc_port", fmt.Sprint(handler.param.RedisServerRpcPort),
		)
	}
	if handler.param.NodeExporterPort != 0 {
		options = append(
			options, "--node_exporter_port", fmt.Sprint(handler.param.NodeExporterPort),
		)
	}
	if handler.param.YcqlServerHttpPort != 0 {
		options = append(
			options, "--ycql_server_http_port", fmt.Sprint(handler.param.YcqlServerHttpPort),
		)
	}
	if handler.param.YcqlServerRpcPort != 0 {
		options = append(
			options, "--ycql_server_rpc_port", fmt.Sprint(handler.param.YcqlServerRpcPort),
		)
	}
	if handler.param.YsqlServerHttpPort != 0 {
		options = append(
			options, "--ysql_server_http_port", fmt.Sprint(handler.param.YsqlServerHttpPort),
		)
	}
	if handler.param.YsqlServerRpcPort != 0 {
		options = append(
			options, "--ysql_server_rpc_port", fmt.Sprint(handler.param.YsqlServerRpcPort),
		)
	}
	if handler.param.YbControllerHttpPort != 0 {
		options = append(
			options, "--yb_controller_http_port", fmt.Sprint(handler.param.YbControllerHttpPort),
		)
	}
	if handler.param.YbControllerRpcPort != 0 {
		options = append(
			options, "--yb_controller_rpc_port", fmt.Sprint(handler.param.YbControllerRpcPort),
		)
	}
	if len(handler.param.MountPaths) > 0 {
		options = append(options, "--mount_points")
		options = append(options, strings.Join(handler.param.MountPaths, ","))
	}
	if handler.param.InstallNodeExporter {
		options = append(options, "--install_node_exporter")
	}
	if handler.param.AirGapInstall {
		options = append(options, "--airgap")
	}
	return options
}
