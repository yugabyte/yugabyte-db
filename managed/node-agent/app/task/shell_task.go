// Copyright (c) YugaByte, Inc.

package task

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"node-agent/model"
	"node-agent/util"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"syscall"

	"github.com/olekukonko/tablewriter"
	funk "github.com/thoas/go-funk"
)

var (
	envFiles     = []string{".bashrc", ".bash_profile"}
	envRegex     = regexp.MustCompile("[A-Za-z_0-9]+=.*")
	redactParams = map[string]bool{
		"jwt":       true,
		"api_token": true,
		"password":  true,
	}
)

// ShellTask handles command execution.
type ShellTask struct {
	// Name of the task.
	name string
	cmd  string
	user string
	args []string
	done bool
}

// NewShellTask returns a shell task executor.
func NewShellTask(name string, cmd string, args []string) *ShellTask {
	return &ShellTask{name: name, cmd: cmd, args: args}
}

// NewShellTaskWithUser returns a shell task executor.
func NewShellTaskWithUser(name string, user string, cmd string, args []string) *ShellTask {
	return &ShellTask{name: name, user: user, cmd: cmd, args: args}
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

func (s *ShellTask) command(name string, arg ...string) (*exec.Cmd, error) {
	cmd := exec.Command(name, arg...)
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
		util.FileLogger().Infof("Using user: %s, uid: %d, gid: %d",
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
	os.Setenv("USER", userAcc.Username)
	os.Setenv("HOME", pwd)
	cmd.Dir = pwd
	return cmd, nil
}

func (s *ShellTask) userEnv(homeDir string) []string {
	var out bytes.Buffer
	env := []string{}
	bashArgs := []string{}
	for _, envFile := range envFiles {
		file := filepath.Join(homeDir, envFile)
		bashArgs = append(bashArgs, fmt.Sprintf("source %s 2> /dev/null", file))
	}
	bashArgs = append(bashArgs, "env")
	cmd, err := s.command("bash", "-c", strings.Join(bashArgs, ";"))
	cmd.Stdout = &out
	err = cmd.Run()
	if err != nil {
		util.FileLogger().Warnf(
			"Failed to source files %v in %s. Error: %s", envFiles, homeDir, err.Error())
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
func (s *ShellTask) Command(name string, arg ...string) (*exec.Cmd, error) {
	cmd, err := s.command(name, arg...)
	if err != nil {
		util.FileLogger().Warnf("Failed to create command %s. Error: %s", name, err.Error())
		return nil, err
	}
	cmd.Env = append(cmd.Env, s.userEnv(cmd.Dir)...)
	return cmd, nil
}

// Process runs the the command Task.
func (s *ShellTask) Process(ctx context.Context) (string, error) {
	var out bytes.Buffer
	var errOut bytes.Buffer
	var output string
	util.FileLogger().Debugf("Starting the command - %s", s.name)
	cmd, err := s.Command(s.cmd, s.args...)
	if err != nil {
		util.FileLogger().Errorf("Command creation for %s failed - %s", s.name, err.Error())
		return out.String(), err
	}
	cmd.Stdout = &out
	cmd.Stderr = &errOut
	redactedArgs := s.redactCommandArgs(s.args...)
	util.FileLogger().Infof("Running command %s with args %v", s.cmd, redactedArgs)
	err = cmd.Run()
	if err != nil {
		output = fmt.Sprintf("%s: %s", err.Error(), errOut.String())
		util.FileLogger().Errorf("Command run - %s task failed - %s", s.name, err.Error())
		util.FileLogger().Errorf("Command output for %s - %s", s.name, output)
	} else {
		output = out.String()
		util.FileLogger().Debugf("Command Run - %s task successful", s.name)
		util.FileLogger().Debugf("command output %s", output)
	}
	s.done = true
	return output, err
}

// Done reports if the command execution is done.
func (s *ShellTask) Done() bool {
	return s.done
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
	util.FileLogger().Debug("Starting Preflight checks handler.")
	var err error
	preflightScriptPath := util.PreflightCheckPath()
	shellCmdTask := NewShellTask(
		"runPreflightCheckScript",
		util.DefaultShell,
		handler.getOptions(preflightScriptPath),
	)
	output, err := shellCmdTask.Process(ctx)
	if err != nil {
		util.FileLogger().Errorf("Pre-flight checks processing failed - %s", err.Error())
		return nil, err
	}
	handler.result = &map[string]model.PreflightCheckVal{}
	err = json.Unmarshal([]byte(output), handler.result)
	if err != nil {
		util.FileLogger().Errorf("Pre-flight checks unmarshaling error - %s", err.Error())
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

func HandleUpgradeScript(config *util.Config, ctx context.Context, version string) error {
	util.FileLogger().Debug("Initializing the upgrade script")
	upgradeScriptTask := NewShellTask(
		"upgradeScript",
		util.DefaultShell,
		[]string{
			util.UpgradeScriptPath(),
			"--type",
			"upgrade",
			"--version",
			version,
		},
	)
	errStr, err := upgradeScriptTask.Process(ctx)
	if err != nil {
		return errors.New(errStr)
	}
	return nil
}

// Shell task process for downloading the node-agent build package.
func HandleDownloadPackageScript(config *util.Config, ctx context.Context) (string, error) {
	util.FileLogger().Debug("Initializing the download package script")
	jwtToken, err := util.GenerateJWT(config)
	if err != nil {
		util.FileLogger().Errorf("Failed to generate JWT during upgrade - %s", err.Error())
		return "", err
	}
	downloadPackageScript := NewShellTask(
		"downloadPackageScript",
		util.DefaultShell,
		[]string{
			util.InstallScriptPath(),
			"--type",
			"download_package",
			"--url",
			config.String(util.PlatformUrlKey),
			"--jwt",
			jwtToken,
		},
	)
	return downloadPackageScript.Process(ctx)
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
