// Copyright (c) YugaByte, Inc.

package task

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/exec"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"

	"github.com/creack/pty"
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

var (
	redactParams = map[string]bool{
		"jwt":       true,
		"api_token": true,
		"password":  true,
	}
	userVariables = []string{"LOGNAME", "USER", "LNAME", "USERNAME"}
)

// ShellTask handles command execution.
type ShellTask struct {
	// Name of the task.
	name     string
	cmd      string
	user     string
	args     []string
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
		name:     name,
		user:     user,
		cmd:      cmd,
		args:     args,
		exitCode: &atomic.Value{},
		stdout:   util.NewBuffer(MaxBufferCapacity),
		stderr:   util.NewBuffer(MaxBufferCapacity),
	}
}

// TaskName returns the name of the shell task.
func (s *ShellTask) TaskName() string {
	return s.name
}

func (s *ShellTask) redactCommandArgs(args ...string) []string {
	redacted := []string{}
	redactValue := false
	for _, param := range args {
		if strings.HasPrefix(param, "-") {
			if _, ok := redactParams[strings.TrimLeft(param, "-")]; ok {
				redactValue = true
			} else {
				redactValue = false
			}
			redacted = append(redacted, param)
		} else if redactValue {
			redacted = append(redacted, "REDACTED")
		} else {
			redacted = append(redacted, param)
		}
	}
	return redacted
}

func (s *ShellTask) command(
	ctx context.Context,
	userDetail *util.UserDetail,
	name string,
	arg ...string,
) (*exec.Cmd, error) {
	cmd := exec.CommandContext(ctx, name, arg...)
	if !userDetail.IsCurrent {
		cmd.SysProcAttr = &syscall.SysProcAttr{}
		cmd.SysProcAttr.Credential = &syscall.Credential{
			Uid: userDetail.UserID,
			Gid: userDetail.GroupID,
		}
	}
	pwd := userDetail.User.HomeDir
	if pwd == "" {
		pwd = "/tmp"
	}
	os.Setenv("PWD", pwd)
	os.Setenv("HOME", pwd)
	for _, userVar := range userVariables {
		os.Setenv(userVar, userDetail.User.Username)
	}
	cmd.Dir = pwd
	return cmd, nil
}

func (s *ShellTask) userEnv(ctx context.Context, userDetail *util.UserDetail) ([]string, error) {
	// Approximate capacity of 100.
	env := make([]string, 0, 100)
	// Interactive shell to source ~/.bashrc.
	cmd, err := s.command(ctx, userDetail, "bash")
	// Create a pseudo tty (non stdin) to act like SSH login.
	// Otherwise, the child process is stopped because it is a background process.
	ptty, err := pty.Start(cmd)
	if err != nil {
		// Return the default env.
		env = append(env, os.Environ()...)
		util.FileLogger().Warnf(
			ctx, "Failed to run command to get env variables. Error: %s", err.Error())
		return env, err
	}
	defer ptty.Close()
	// Do not write to history file.
	ptty.Write([]byte("unset HISTFILE >/dev/null 2>&1\n"))
	// Run env and end each output line with NULL instead of newline.
	ptty.Write([]byte("env -0 2>/dev/null\n"))
	ptty.Write([]byte("exit 0\n"))
	// End of transmission.
	ptty.Write([]byte{4})
	scanner := bufio.NewScanner(ptty)
	// Custom delimiter to tokenize at NULL byte.
	scanner.Split(func(data []byte, atEOF bool) (int, []byte, error) {
		if atEOF && len(data) == 0 {
			return 0, nil, nil
		}
		var token []byte
		for i, b := range data {
			if b == 0 {
				// NULL marker is found.
				return i + 1, token, nil
			}
			if b != '\r' {
				if token == nil {
					token = make([]byte, 0, len(data))
				}
				token = append(token, b)
			}
		}
		if atEOF {
			return len(data), token, nil
		}
		return 0, nil, nil
	})
	beginOutput := false
	for scanner.Scan() {
		entry := scanner.Text()
		sepIdx := strings.Index(entry, "=")
		if !beginOutput {
			if sepIdx > 0 {
				// Remove any preceding input commands followed by newline
				// as STDIN and STDOUT are the same.
				nIdx := strings.LastIndex(entry[:sepIdx], "\n")
				if nIdx >= 0 {
					entry = entry[nIdx+1:]
				}
			}
			beginOutput = true
		}
		if sepIdx > 0 {
			env = append(env, entry)
		}
	}
	if util.FileLogger().IsDebugEnabled() {
		util.FileLogger().Debugf(ctx, "Env: %v", env)
	}
	err = cmd.Wait()
	if err != nil {
		// Return the default env.
		env = append(env, os.Environ()...)
		util.FileLogger().Warnf(
			ctx, "Failed to get env variables. Error: %s", err.Error())
		return env, err
	}
	return env, nil
}

// Command returns a command with the environment set.
func (s *ShellTask) Command(ctx context.Context, name string, arg ...string) (*exec.Cmd, error) {
	userDetail, err := util.UserInfo(s.user)
	if err != nil {
		return nil, err
	}
	util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
		userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
	env, _ := s.userEnv(ctx, userDetail)
	cmd, err := s.command(ctx, userDetail, name, arg...)
	if err != nil {
		util.FileLogger().Warnf(ctx, "Failed to create command %s. Error: %s", name, err.Error())
		return nil, err
	}
	cmd.Env = append(cmd.Env, env...)
	return cmd, nil
}

// Process runs the the command Task.
func (s *ShellTask) Process(ctx context.Context) (*TaskStatus, error) {
	util.FileLogger().Debugf(ctx, "Starting the command - %s", s.name)
	taskStatus := &TaskStatus{Info: s.stdout, ExitStatus: &ExitStatus{Code: 1, Error: s.stderr}}
	cmd, err := s.Command(ctx, s.cmd, s.args...)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Command creation for %s failed - %s", s.name, err.Error())
		return taskStatus, err
	}
	cmd.Stdout = s.stdout
	cmd.Stderr = s.stderr
	if util.FileLogger().IsDebugEnabled() {
		redactedArgs := s.redactCommandArgs(s.args...)
		util.FileLogger().Debugf(ctx, "Running command %s with args %v", s.cmd, redactedArgs)
	}
	err = cmd.Run()
	if err == nil {
		taskStatus.Info = s.stdout
		taskStatus.ExitStatus.Code = 0
		if util.FileLogger().IsDebugEnabled() {
			util.FileLogger().
				Debugf(ctx, "Command %s executed successfully - %s", s.name, s.stdout.String())
		}
	} else {
		taskStatus.ExitStatus.Error = s.stderr
		if exitErr, ok := err.(*exec.ExitError); ok {
			taskStatus.ExitStatus.Code = exitErr.ExitCode()
		}
		errMsg := fmt.Sprintf("%s: %s", err.Error(), s.stderr.String())
		util.FileLogger().Errorf(ctx, "Command %s execution failed - %s", s.name, errMsg)
	}
	s.exitCode.Store(taskStatus.ExitStatus.Code)
	return taskStatus, err
}

// Handler implements the AsyncTask method.
func (s *ShellTask) Handler() util.Handler {
	return util.Handler(func(ctx context.Context) (any, error) {
		return s.Process(ctx)
	})
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
	return s.cmd
}

// Result returns the result.
func (s *ShellTask) Result() any {
	return nil
}

// CreatePreflightCheckParam returns PreflightCheckParam from the given parameters.
func CreatePreflightCheckParam(
	provider *model.Provider,
	instanceType *model.NodeInstanceType,
	accessKey *model.AccessKey) *model.PreflightCheckParam {
	param := &model.PreflightCheckParam{}
	param.AirGapInstall = provider.AirGapInstall
	param.SkipProvisioning = accessKey.KeyInfo.SkipProvisioning
	param.InstallNodeExporter = accessKey.KeyInfo.InstallNodeExporter
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
	param.AirGapInstall = param.AirGapInstall || accessKey.KeyInfo.AirGapInstall
	return param
}

type PreflightCheckHandler struct {
	shellTask *ShellTask
	param     *model.PreflightCheckParam
	result    *[]model.NodeConfig
}

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

// Handler implements the AsyncTask method.
func (handler *PreflightCheckHandler) Handler() util.Handler {
	return handler.Handle
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

func (handler *PreflightCheckHandler) Handle(ctx context.Context) (any, error) {
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
	return handler.result, nil
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
	if handler.param.MountPaths != nil && len(handler.param.MountPaths) > 0 {
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
