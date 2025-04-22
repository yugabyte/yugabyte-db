// Copyright (c) YugaByte, Inc.

package module

import (
	"bufio"
	"context"
	"node-agent/util"
	"os"
	"os/exec"
	"strings"
	"syscall"

	"github.com/creack/pty"
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

// Command handles command execution.
type Command struct {
	// Name of the command.
	name string
	cmd  string
	user string
	args []string
}

// NewCommand returns a command instance.
func NewCommand(name string, cmd string, args []string) *Command {
	return NewCommandWithUser(name, "", cmd, args)
}

// NewCommandWithUser returns a command instance.
func NewCommandWithUser(name string, user string, cmd string, args []string) *Command {
	return &Command{
		name: name,
		user: user,
		cmd:  cmd,
		args: args,
	}
}

// Name returns the name of the command.
func (command *Command) Name() string {
	return command.name
}

// Cmd returns the command.
func (command *Command) Cmd() string {
	return command.cmd
}

// Args returns the command arguments.
func (command *Command) Args() []string {
	return command.args
}

// RedactCommandArgs redacts the command arguments and returns them.
func (command *Command) RedactCommandArgs() []string {
	redacted := []string{}
	redactValue := false
	for _, param := range command.args {
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

// Create returns the exec command with the environment set.
func (command *Command) Create(ctx context.Context) (*exec.Cmd, error) {
	userDetail, err := util.UserInfo(command.user)
	if err != nil {
		return nil, err
	}
	util.FileLogger().Debugf(ctx, "Using user: %s, uid: %d, gid: %d",
		userDetail.User.Username, userDetail.UserID, userDetail.GroupID)
	env, _ := command.userEnv(ctx, userDetail)
	cmd, err := command.command(ctx, userDetail, command.cmd, command.args...)
	if err != nil {
		util.FileLogger().
			Warnf(ctx, "Failed to create command %s. Error: %s", command.name, err.Error())
		return nil, err
	}
	cmd.Env = append(cmd.Env, env...)
	return cmd, nil
}

func (command *Command) command(
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

func (command *Command) userEnv(
	ctx context.Context,
	userDetail *util.UserDetail,
) ([]string, error) {
	// Approximate capacity of 100.
	env := make([]string, 0, 100)
	// Interactive shell to source ~/.bashrc.
	cmd, err := command.command(ctx, userDetail, "bash")
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
