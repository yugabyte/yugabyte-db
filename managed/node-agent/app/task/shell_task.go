// Copyright (c) YugaByte, Inc.

package task

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/exec"
	"os/user"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"
	"syscall"

	"github.com/olekukonko/tablewriter"
	funk "github.com/thoas/go-funk"
)

const (
	// MaxBufferCapacity is the max number of bytes allowed in the buffer
	// before truncating the first bytes.
	MaxBufferCapacity = 1000000
)

var (
	envRegex     = regexp.MustCompile("[A-Za-z_0-9]+=.*")
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

func (s *ShellTask) command(ctx context.Context, name string, arg ...string) (*exec.Cmd, error) {
	cmd := exec.CommandContext(ctx, name, arg...)
	userAcc, err := user.Current()
	if err != nil {
		return nil, err
	}
	if s.user != "" && userAcc.Username != s.user {
		var uid, gid uint32
		userAcc, uid, gid, err = util.UserInfo(s.user)
		if err != nil {
			return nil, err
		}
		util.FileLogger().Infof(ctx, "Using user: %s, uid: %d, gid: %d",
			userAcc.Username, uid, gid)
		cmd.SysProcAttr = &syscall.SysProcAttr{}
		cmd.SysProcAttr.Credential = &syscall.Credential{
			Uid: uint32(uid),
			Gid: uint32(gid),
		}
	}
	pwd := userAcc.HomeDir
	if pwd == "" {
		pwd = "/tmp"
	}
	os.Setenv("PWD", pwd)
	os.Setenv("HOME", pwd)
	for _, userVar := range userVariables {
		os.Setenv(userVar, userAcc.Username)
	}
	cmd.Dir = pwd
	return cmd, nil
}

func (s *ShellTask) userEnv(ctx context.Context, homeDir string) []string {
	var out bytes.Buffer
	env := []string{}
	// Interactive login and run command.
	cmd, err := s.command(ctx, "bash", "-ilc", "env 2>/dev/null")
	cmd.Stdout = &out
	err = cmd.Run()
	if err != nil {
		util.FileLogger().Warnf(
			ctx, "Failed to get env variables for %s. Error: %s", homeDir, err.Error())
		return env
	}
	env = append(env, os.Environ()...)
	scanner := bufio.NewScanner(bytes.NewReader(out.Bytes()))
	for scanner.Scan() {
		line := scanner.Text()
		if envRegex.MatchString(line) {
			env = append(env, line)
		}
	}
	return env
}

// Command returns a command with the environment set.
func (s *ShellTask) Command(ctx context.Context, name string, arg ...string) (*exec.Cmd, error) {
	cmd, err := s.command(ctx, name, arg...)
	if err != nil {
		util.FileLogger().Warnf(ctx, "Failed to create command %s. Error: %s", name, err.Error())
		return nil, err
	}
	cmd.Env = append(cmd.Env, s.userEnv(ctx, cmd.Dir)...)
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
		util.FileLogger().Infof(ctx, "Running command %s with args %v", s.cmd, redactedArgs)
	}
	err = cmd.Run()
	if err == nil {
		taskStatus.Info = s.stdout
		taskStatus.ExitStatus.Code = 0
		util.FileLogger().
			Debugf(ctx, "Command %s executed successfully - %s", s.name, s.stdout.String())
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

type PreflightCheckHandler struct {
	provider     *model.Provider
	instanceType *model.NodeInstanceType
	accessKey    *model.AccessKey
	result       *map[string]model.PreflightCheckVal
}

func NewPreflightCheckHandler(
	provider *model.Provider,
	instanceType *model.NodeInstanceType,
	accessKey *model.AccessKey,
) *PreflightCheckHandler {
	return &PreflightCheckHandler{
		provider:     provider,
		instanceType: instanceType,
		accessKey:    accessKey,
	}
}

func (handler *PreflightCheckHandler) Handle(ctx context.Context) (any, error) {
	util.FileLogger().Debug(ctx, "Starting Preflight checks handler.")
	var err error
	preflightScriptPath := util.PreflightCheckPath()
	shellCmdTask := NewShellTask(
		"runPreflightCheckScript",
		util.DefaultShell,
		handler.getOptions(preflightScriptPath),
	)
	output, err := shellCmdTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Pre-flight checks processing failed - %s", err.Error())
		return nil, err
	}
	data := output.Info.String()
	util.FileLogger().Debugf(ctx, "Preflight check output data: %s", data)
	handler.result = &map[string]model.PreflightCheckVal{}
	err = json.Unmarshal([]byte(data), handler.result)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Pre-flight checks unmarshaling error - %s", err.Error())
		return nil, err
	}
	return handler.result, nil
}

func (handler *PreflightCheckHandler) Result() *map[string]model.PreflightCheckVal {
	return handler.result
}

// Returns options for the preflight checks.
func (handler *PreflightCheckHandler) getOptions(preflightScriptPath string) []string {
	provider := handler.provider
	instanceType := handler.instanceType
	accessKey := handler.accessKey
	options := make([]string, 3)
	options[0] = preflightScriptPath
	options[1] = "-t"

	if accessKey.KeyInfo.SkipProvisioning {
		options[2] = "configure"
	} else {
		options[2] = "provision"
	}

	if provider.AirGapInstall {
		options = append(options, "--airgap")
	}

	if homeDir, ok := provider.Config["YB_HOME_DIR"]; ok {
		options = append(options, "--yb_home_dir", "'"+homeDir+"'")
	} else {
		options = append(options, "--yb_home_dir", util.NodeHomeDirectory)
	}

	if data := provider.SshPort; data != 0 {
		options = append(options, "--ssh_port", fmt.Sprint(data))
	}

	if data := instanceType.Details.VolumeDetailsList; len(data) > 0 {
		options = append(options, "--mount_points")
		mp := ""
		for i, volumeDetail := range data {
			mp += volumeDetail.MountPath
			if i < len(data)-1 {
				mp += ","
			}
		}
		options = append(options, mp)
	}
	if accessKey.KeyInfo.InstallNodeExporter {
		options = append(options, "--install_node_exporter")
	}
	if accessKey.KeyInfo.AirGapInstall {
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
