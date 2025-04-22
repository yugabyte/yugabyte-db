// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"encoding/json"
	"fmt"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/exec"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"

	"github.com/olekukonko/tablewriter"
	funk "github.com/thoas/go-funk"
)

const (
	// MaxBufferCapacity is the max number of bytes allowed in the buffer
	// before truncating the first bytes.
	MaxBufferCapacity = 1000000
)

const (
	mountPointsVolume    = "mount_points_volume"
	mountPointsWritable  = "mount_points_writable"
	masterHTTPPort       = "master_http_port"
	masterRPCPort        = "master_rpc_port"
	tserverHTTPPort      = "tserver_http_port"
	tserverRPCPort       = "tserver_rpc_port"
	ybControllerHTTPPort = "yb_controller_http_port"
	ybControllerRPCPort  = "yb_controller_rpc_port"
	redisServerHTTPPort  = "redis_server_http_port"
	redisServerRPCPort   = "redis_server_rpc_port"
	ycqlServerHTTPPort   = "ycql_server_http_port"
	ycqlServerRPCPort    = "ycql_server_rpc_port"
	ysqlServerHTTPPort   = "ysql_server_http_port"
	ysqlServerRPCPort    = "ysql_server_rpc_port"
	sshPort              = "ssh_port"
	nodeExporterPort     = "node_exporter_port"
)

// ShellTask handles command execution using module.Command.
type ShellTask struct {
	// Name of the task.
	command  *module.Command
	stdout   util.Buffer
	stderr   util.Buffer
	exitCode *atomic.Value
}

// NewShellTask returns a shell task executor.
func NewShellTask(name string, cmd string, args []string) *ShellTask {
	return NewShellTaskWithUser(name, "", cmd, args)
}

// NewShellTaskWithUser returns a shell task executor.
func NewShellTaskWithUser(name string, user string, cmd string, args []string) *ShellTask {
	return &ShellTask{
		command:  module.NewCommandWithUser(name, user, cmd, args),
		exitCode: &atomic.Value{},
		stdout:   util.NewBuffer(MaxBufferCapacity),
		stderr:   util.NewBuffer(MaxBufferCapacity),
	}
}

// TaskName returns the name of the shell task.
func (s *ShellTask) TaskName() string {
	return s.command.Name()
}

// Process runs the the command Task.
func (s *ShellTask) Process(ctx context.Context) (*TaskStatus, error) {
	util.FileLogger().Debugf(ctx, "Starting the command - %s", s.command.Name())
	taskStatus := &TaskStatus{Info: s.stdout, ExitStatus: &ExitStatus{Code: 1, Error: s.stderr}}
	cmd, err := s.command.Create(ctx)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Command creation for %s failed - %s", s.command.Name(), err.Error())
		return taskStatus, err
	}
	cmd.Stdout = s.stdout
	cmd.Stderr = s.stderr
	if util.FileLogger().IsDebugEnabled() {
		redactedArgs := s.command.RedactCommandArgs()
		util.FileLogger().
			Debugf(ctx, "Running command %s with args %v", s.command.Cmd(), redactedArgs)
	}
	err = cmd.Run()
	if err == nil {
		taskStatus.Info = s.stdout
		taskStatus.ExitStatus.Code = 0
		if util.FileLogger().IsDebugEnabled() {
			util.FileLogger().
				Debugf(ctx, "Command %s executed successfully - %s", s.command.Name(), s.stdout.String())
		}
	} else {
		taskStatus.ExitStatus.Error = s.stderr
		if exitErr, ok := err.(*exec.ExitError); ok {
			taskStatus.ExitStatus.Code = exitErr.ExitCode()
		}
		if util.FileLogger().IsDebugEnabled() && s.stdout.Len() > 0 {
			util.FileLogger().
				Debugf(ctx, "Output for failed command %s - %s", s.command.Name(), s.stdout.String())
		}
		errMsg := fmt.Sprintf("%s: %s", err.Error(), s.stderr.String())
		util.FileLogger().Errorf(ctx, "Command %s execution failed - %s", s.command.Name(), errMsg)
	}
	s.exitCode.Store(taskStatus.ExitStatus.Code)
	return taskStatus, err
}

// Handle implements the AsyncTask method.
func (s *ShellTask) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	_, err := s.Process(ctx)
	return nil, err
}

// CurrentTaskStatus implements the AsyncTask method.
func (s *ShellTask) CurrentTaskStatus() *TaskStatus {
	v := s.exitCode.Load()
	if v == nil {
		return &TaskStatus{
			Info: s.stdout,
		}
	}
	return &TaskStatus{
		Info: s.stdout,
		ExitStatus: &ExitStatus{
			Code:  v.(int),
			Error: s.stderr,
		},
	}
}

// String implements the AsyncTask method.
func (s *ShellTask) String() string {
	return s.command.Name()
}

// Result returns the result.
func (s *ShellTask) Result() any {
	return nil
}

// CreatePreflightCheckParam returns PreflightCheckParam from the given parameters.
func CreatePreflightCheckParam(
	provider *model.Provider,
	instanceType *model.NodeInstanceType) *model.PreflightCheckParam {
	param := &model.PreflightCheckParam{}
	param.AirGapInstall = provider.AirGapInstall
	param.SkipProvisioning = provider.Details.SkipProvisioning
	param.InstallNodeExporter = provider.Details.InstallNodeExporter
	param.YbHomeDir = util.NodeHomeDirectory
	if homeDir, ok := provider.Config["YB_HOME_DIR"]; ok {
		param.YbHomeDir = homeDir
	}
	param.NodeExporterPort = provider.Details.NodeExporterPort
	param.SshPort = provider.SshPort
	if data := instanceType.Details.VolumeDetailsList; len(data) > 0 {
		param.MountPaths = make([]string, len(data))
		for i, volumeDetail := range data {
			param.MountPaths[i] = volumeDetail.MountPath
		}
	}
	param.AirGapInstall = param.AirGapInstall || provider.Details.AirGapInstall
	return param
}

func HandleUpgradeScript(ctx context.Context, config *util.Config) error {
	util.FileLogger().Debug(ctx, "Initializing the upgrade script")
	upgradeScriptTask := NewShellTask(
		"upgradeScript",
		util.DefaultShell,
		[]string{
			util.UpgradeScriptPath(),
			"--command",
			"upgrade",
			"--install_path",
			util.InstallDir(),
		},
	)
	_, err := upgradeScriptTask.Process(ctx)
	if err != nil {
		return err
	}
	version, err := util.Version()
	if err != nil {
		return err
	}
	return config.Update(util.PlatformVersionUpdateKey, version)
}

func OutputPreflightCheck(responses map[string]model.NodeInstanceValidationResponse) bool {
	table := tablewriter.NewWriter(os.Stdout)
	table.SetHeader([]string{"Preflight Check", "Value", "Description", "Required", "Result"})
	table.SetAlignment(tablewriter.ALIGN_LEFT)
	table.SetAutoWrapText(false)
	table.SetRowLine(true)
	table.SetHeaderColor(
		tablewriter.Colors{},
		tablewriter.Colors{},
		tablewriter.Colors{},
		tablewriter.Colors{},
		tablewriter.Colors{},
	)
	keys := funk.Keys(responses).([]string)
	sort.Strings(keys)
	allValid := true
	for _, k := range keys {
		v := responses[k]
		if v.Valid {
			data := []string{k, v.Value, v.Description, strconv.FormatBool(v.Required), "Passed"}
			table.Rich(
				data,
				[]tablewriter.Colors{
					tablewriter.Colors{tablewriter.FgGreenColor},
					tablewriter.Colors{tablewriter.FgGreenColor},
					tablewriter.Colors{tablewriter.FgGreenColor},
					tablewriter.Colors{tablewriter.FgGreenColor},
					tablewriter.Colors{tablewriter.FgGreenColor},
				},
			)
		} else {
			if v.Required {
				allValid = false
			}
			data := []string{k, v.Value, v.Description, strconv.FormatBool(v.Required), "Failed"}
			table.Rich(data, []tablewriter.Colors{
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor},
				tablewriter.Colors{tablewriter.FgRedColor},
			})
		}
	}
	table.Render()
	return allValid
}

func getNodeConfig(data map[string]model.PreflightCheckVal) *[]model.NodeConfig {
	mountPointsWritableMap := make(map[string]string)
	mountPointsVolumeMap := make(map[string]string)
	result := make([]model.NodeConfig, 0)
	for k, v := range data {
		kSplit := strings.Split(k, ":")
		switch kSplit[0] {
		case mountPointsWritable:
			mountPointsWritableMap[kSplit[1]] = v.Value
		case mountPointsVolume:
			mountPointsVolumeMap[kSplit[1]] = v.Value
		case masterHTTPPort, masterRPCPort, tserverHTTPPort, tserverRPCPort,
			ybControllerHTTPPort, ybControllerRPCPort, redisServerHTTPPort,
			redisServerRPCPort, ycqlServerHTTPPort, ycqlServerRPCPort,
			ysqlServerHTTPPort, ysqlServerRPCPort, sshPort, nodeExporterPort:
			portMap := make(map[string]string)
			portMap[kSplit[1]] = v.Value
			result = appendMap(kSplit[0], portMap, result)
		default:
			// Try Getting Python Version.
			vSplit := strings.Split(v.Value, " ")
			if len(vSplit) > 0 && strings.EqualFold(vSplit[0], "Python") {
				result = append(
					result,
					model.NodeConfig{Type: strings.ToUpper(kSplit[0]), Value: vSplit[1]},
				)
			} else {
				result = append(result, model.NodeConfig{Type: strings.ToUpper(kSplit[0]), Value: v.Value})
			}
		}
	}

	// Marshal the existence of mount points in the request.
	result = appendMap(mountPointsWritable, mountPointsWritableMap, result)

	// Marshal the mount points volume in the request.
	result = appendMap(mountPointsVolume, mountPointsVolumeMap, result)

	return &result
}

// Marshal helper function for maps.
func appendMap(key string, valMap map[string]string, result []model.NodeConfig) []model.NodeConfig {
	if len(valMap) > 0 {
		valJSON, err := json.Marshal(valMap)
		if err != nil {
			panic("Error while marshaling map")
		}
		return append(
			result,
			model.NodeConfig{
				Type:  strings.ToUpper(key),
				Value: string(valJSON),
			},
		)
	}

	return result
}
