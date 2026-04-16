// Copyright (c) YugabyteDB, Inc.

package module

import (
	"bufio"
	"context"
	"fmt"
	"node-agent/backoff"
	"node-agent/util"
	"os"
	"os/exec"
	"strings"
	"syscall"

	"github.com/creack/pty"
)

const (
	// MaxBufferCapacity is the max number of bytes allowed in the buffer
	// before truncating the first bytes.
	MaxBufferCapacity = 1000000
)

var (
	// Parameters in the command that should be redacted.
	redactParams = map[string]bool{
		"jwt":       true,
		"api_token": true,
		"password":  true,
	}
	userVariables = []string{"LOGNAME", "USER", "LNAME", "USERNAME"}
)

// CommandInfo holds command information.
type CommandInfo struct {
	User   string
	Desc   string
	Cmd    string
	Args   []string
	StdOut util.Buffer
	StdErr util.Buffer
}

// RedactCommandArgs redacts the command arguments and returns them.
func (cmdInfo *CommandInfo) RedactCommandArgs() []string {
	redacted := []string{}
	redactValue := false
	for _, param := range cmdInfo.Args {
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

// RunCmd runs the command in the command info.
func (cmdInfo *CommandInfo) RunCmd(ctx context.Context) error {
	userDetail, err := util.UserInfo(cmdInfo.User)
	if err != nil {
		return err
	}
	util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
		userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
	env, _ := userEnv(ctx, userDetail)
	cmd, err := createCmd(ctx, userDetail, cmdInfo.Cmd, cmdInfo.Args...)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to create command %s. Error: %s", cmdInfo.Desc, err.Error())
		return err
	}
	cmd.Env = append(cmd.Env, env...)
	if cmdInfo.StdOut != nil {
		cmd.Stdout = cmdInfo.StdOut
	}
	if cmdInfo.StdErr != nil {
		cmd.Stderr = cmdInfo.StdErr
	}
	return cmd.Run()
}

// RunSteps runs a list of command steps with the specified user and logs the output.
func RunSteps(
	ctx context.Context,
	user string,
	steps []struct {
		Desc string
		Cmd  string
		Args []string
	}, logOut util.Buffer) error {
	cmdInfos := make([]*CommandInfo, len(steps))
	for i, step := range steps {
		cmdInfos[i] = &CommandInfo{
			User:   user,
			Desc:   step.Desc,
			Cmd:    step.Cmd,
			Args:   step.Args,
			StdOut: util.NewBuffer(MaxBufferCapacity),
			StdErr: util.NewBuffer(MaxBufferCapacity),
		}
	}
	return createCmds(
		ctx,
		user,
		cmdInfos,
		func(ctx context.Context, cmdInfo *CommandInfo, cmd *exec.Cmd) error {
			if logOut != nil {
				logOut.WriteLine("Running step: %s", cmdInfo.Desc)
			}
			err := cmd.Run()
			if err != nil {
				errMsg := fmt.Sprintf("%s - %s", err.Error(), cmdInfo.StdErr.String())
				util.FileLogger().
					Errorf(ctx, "Failed to run step %s: %s", cmdInfo.Desc, errMsg)
				if logOut != nil {
					logOut.WriteLine("Failed to run step %s: %s", cmdInfo.Desc, errMsg)
				}
			}
			return err
		})
}

// RunShellCmd runs a shell command with the specified user.
func RunShellCmd(
	ctx context.Context,
	user, desc, cmdStr string,
	logOut util.Buffer,
) (*CommandInfo, error) {
	cmdInfo := &CommandInfo{
		User:   user,
		Desc:   desc,
		Cmd:    util.DefaultShell,
		Args:   []string{"-c", cmdStr},
		StdOut: util.NewBuffer(MaxBufferCapacity),
		StdErr: util.NewBuffer(MaxBufferCapacity),
	}
	if logOut != nil {
		logOut.WriteLine("Running shell command for %s", desc)
	}
	err := cmdInfo.RunCmd(ctx)
	if err != nil {
		errMsg := fmt.Sprintf("%s - %s", err.Error(), cmdInfo.StdErr.String())
		util.FileLogger().
			Errorf(ctx, "Failed to run shell command for %s: %s", desc, errMsg)
		if logOut != nil {
			logOut.WriteLine("Failed to run shell command for %s: %s", desc, errMsg)
		}
	}
	return cmdInfo, err
}

// RunShellSteps runs a list of shell command steps with the specified user and logs the output.
func RunShellSteps(
	ctx context.Context,
	user string,
	steps []struct {
		Desc string
		Cmd  string
	}, logOut util.Buffer) error {
	cmdSteps := make([]struct {
		Desc string
		Cmd  string
		Args []string
	}, len(steps))
	for i, step := range steps {
		cmdSteps[i] = struct {
			Desc string
			Cmd  string
			Args []string
		}{step.Desc, util.DefaultShell, []string{"-c", step.Cmd}}
	}
	return RunSteps(
		ctx,
		user,
		cmdSteps, logOut)
}

func createCmd(
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

func RunShellCmdWithRetry(
	ctx context.Context,
	backOff backoff.BackOff,
	user, desc, cmdStr string,
	logOut util.Buffer,
) (*CommandInfo, error) {
	var cmdInfo *CommandInfo
	var err error
	if err = backoff.Do(ctx, backOff, func(attempt int) error {
		util.FileLogger().Infof(ctx, "Running %s command: %s", desc, cmdStr)
		if logOut != nil {
			logOut.WriteLine("Running %s command: %s", desc, cmdStr)
		}
		cmdInfo, err = RunShellCmd(ctx, user, desc, cmdStr, logOut)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Failed to run %s command: %s - %s", desc, cmdStr, err.Error())
			if logOut != nil {
				logOut.WriteLine("Failed to run %s command: %s - %s", desc, cmdStr, err.Error())
			}
			return err
		}
		util.FileLogger().Infof(ctx, "%s command %s succeeded in %d attempts", desc, cmdStr, attempt)
		if logOut != nil {
			logOut.WriteLine("%s command %s succeeded in %d attempts", desc, cmdStr, attempt)
		}
		return nil
	}); err != nil {
		return nil, err
	}
	return cmdInfo, nil
}

func RunShellStepsWithRetry(
	ctx context.Context,
	backOff backoff.BackOff,
	user string,
	steps []struct {
		Desc string
		Cmd  string
	},
	logOut util.Buffer,
) ([]*CommandInfo, error) {
	cmdInfos := make([]*CommandInfo, len(steps))
	for i, step := range steps {
		if err := backoff.Do(ctx, backOff, func(attempt int) error {
			util.FileLogger().Infof(ctx, "Running %s command: %s", step.Desc, step.Cmd)
			if logOut != nil {
				logOut.WriteLine("Running %s command: %s", step.Desc, step.Cmd)
			}
			cmdInfo, err := RunShellCmd(ctx, user, step.Desc, step.Cmd, logOut)
			if err != nil {
				util.FileLogger().Errorf(ctx, "Failed to run %s command: %s - %s", step.Desc, step.Cmd, err.Error())
				if logOut != nil {
					logOut.WriteLine("Failed to run %s command: %s - %s", step.Desc, step.Cmd, err.Error())
				}
				return err
			}
			cmdInfos[i] = cmdInfo
			util.FileLogger().Infof(ctx, "%s command %s succeeded in %d attempts", step.Desc, step.Cmd, attempt)
			if logOut != nil {
				logOut.WriteLine("%s command %s succeeded in %d attempts", step.Desc, step.Cmd, attempt)
			}
			return nil
		}); err != nil {
			return cmdInfos, err
		}
	}
	return cmdInfos, nil
}

// createCmds creates commands from the command info list and passes them to the receiver.
func createCmds(
	ctx context.Context,
	user string,
	infos []*CommandInfo,
	receiver func(context.Context, *CommandInfo, *exec.Cmd) error,
) error {
	userDetail, err := util.UserInfo(user)
	if err != nil {
		return err
	}
	util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
		userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
	env, _ := userEnv(ctx, userDetail)
	for _, info := range infos {
		cmd, err := createCmd(ctx, userDetail, info.Cmd, info.Args...)
		if err != nil {
			util.FileLogger().
				Warnf(ctx, "Failed to create command %s. Error: %s", info.Desc, err.Error())
			return err
		}
		cmd.Env = append(cmd.Env, env...)
		if info.StdOut != nil {
			cmd.Stdout = info.StdOut
		}
		if info.StdErr != nil {
			cmd.Stderr = info.StdErr
		}
		err = receiver(ctx, info, cmd)
		if err != nil {
			return err
		}
	}
	return nil
}

func userEnv(
	ctx context.Context,
	userDetail *util.UserDetail,
) ([]string, error) {
	// Approximate capacity of 100.
	env := make([]string, 0, 100)
	// Interactive shell to source ~/.bashrc.
	cmd, err := createCmd(ctx, userDetail, "bash")
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
	if util.FileLogger().IsDebugEnabled(ctx) {
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
