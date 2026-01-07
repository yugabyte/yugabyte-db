// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/backoff"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	UserSystemdUnitPath   = ".config/systemd/user"
	ServerTemplateSubpath = "server/"
)

var (
	SystemdBackOff = backoff.NewSimpleBackOff(10*time.Second /* interval */, 10 /* max attempts */)
	// UserSystemdUnitsForUpdate maps process names to their service file source and destination paths.
	UserSystemdUnitsForUpdate = map[string]struct {
		Src  string
		Dest string
	}{
		"master":     {"yb-master.service", UserSystemdUnitPath + "/yb-master.service"},
		"tserver":    {"yb-tserver.service", UserSystemdUnitPath + "/yb-tserver.service"},
		"controller": {"yb-controller.service", UserSystemdUnitPath + "/yb-controller.service"},
	}
)

// IsUserSystemd checks if the systemd service file exists in the user's systemd directory.
// It returns true along with the path if it exists, false otherwise.
func IsUserSystemd(username, serverName string) (bool, string, error) {
	info, err := util.UserInfo(username)
	if err != nil {
		return false, "", err
	}
	if !strings.HasSuffix(serverName, ".service") && !strings.HasSuffix(serverName, ".timer") {
		serverName = serverName + ".service"
	}
	path := filepath.Join(info.User.HomeDir, ".config/systemd/user", serverName)
	_, err = os.Stat(path)
	if err == nil {
		return true, path, nil
	}
	if errors.Is(err, os.ErrNotExist) {
		return false, "", nil
	}
	return false, "", err
}

func getUserOptionForUserLevel(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) (string, string, error) {
	userOption := ""
	cmdUser := ""
	if username != "" {
		yes, _, err := IsUserSystemd(username, serverName)
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
			logOut.WriteLine(
				"Failed to get user option for systemd: %s - %s",
				serverName,
				err.Error(),
			)
			return userOption, cmdUser, err
		}
		if yes {
			userOption = "--user "
			cmdUser = username
		}
	}
	return userOption, cmdUser, nil
}

func EnableSystemdService(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		return err
	}
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{"ReloadSystemdDaemon", fmt.Sprintf("systemctl %sdaemon-reload", userOption)},
		{"EnableSystemdService", fmt.Sprintf("systemctl %senable %s", userOption, serverName)},
	}
	_, err = RunShellStepsWithRetry(ctx, SystemdBackOff, cmdUser, steps, logOut)
	return err
}

func StartSystemdService(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		return err
	}
	cmd := fmt.Sprintf("systemctl %sstart %s", userOption, serverName)
	_, err = RunShellCmdWithRetry(
		ctx,
		SystemdBackOff,
		cmdUser,
		"StartSystemdService",
		cmd,
		logOut,
	)
	return err
}

func StopSystemdService(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		return err
	}
	cmd := fmt.Sprintf("systemctl %s stop %s", userOption, serverName)
	_, err = RunShellCmdWithRetry(ctx, SystemdBackOff, cmdUser, "StopSystemdService", cmd, logOut)
	return err
}

func DisableSystemdService(
	ctx context.Context,
	username, serverName, unitPathToBeRemoved string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		return err
	}
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{"ReloadSystemdDaemon", fmt.Sprintf("systemctl %sdaemon-reload", userOption)},
		{"StopSystemdService", fmt.Sprintf("systemctl %sstop %s", userOption, serverName)},
		{"DisableSystemdService", fmt.Sprintf("systemctl %sdisable %s", userOption, serverName)},
	}
	_, err = RunShellStepsWithRetry(ctx, SystemdBackOff, cmdUser, steps, logOut)
	if err != nil {
		return err
	}
	if unitPathToBeRemoved != "" {
		util.FileLogger().Infof(ctx, "Removing systemd unit file %s", unitPathToBeRemoved)
		logOut.WriteLine("Removing systemd unit file %s", unitPathToBeRemoved)
		if err := os.Remove(unitPathToBeRemoved); err != nil && !os.IsNotExist(err) {
			util.FileLogger().
				Errorf(ctx, "Failed to remove systemd unit file %s - %s", unitPathToBeRemoved, err.Error())
			logOut.WriteLine(
				"Failed to remove systemd unit file %s - %s",
				unitPathToBeRemoved,
				err.Error(),
			)
			return err
		}
	}
	cmd := fmt.Sprintf("systemctl %sdaemon-reload", userOption)
	_, err = RunShellCmdWithRetry(
		ctx,
		SystemdBackOff,
		cmdUser,
		"ReloadSystemdDaemon",
		cmd,
		logOut,
	)
	return err
}

func ControlSystemdService(
	ctx context.Context,
	username, serverName, controlType string,
	logOut util.Buffer,
) error {
	if strings.HasSuffix(controlType, "start") {
		err := EnableSystemdService(ctx, username, serverName, logOut)
		if err != nil {
			return err
		}
		if controlType == "restart" {
			err = StopSystemdService(ctx, username, serverName, logOut)
			if err != nil {
				return err
			}
		}
		err = StartSystemdService(ctx, username, serverName, logOut)
		if err != nil {
			return err
		}
	} else if controlType == "stop" {
		err := StopSystemdService(ctx, username, serverName, logOut)
		if err != nil {
			return err
		}
		err = DisableSystemdService(ctx, username, serverName, "", logOut)
		if err != nil {
			return err
		}
	} else {
		return fmt.Errorf("Unsupported control type for systemd service: %s", controlType)
	}
	return nil
}

func SystemdUnitPath(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) (string, error) {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		return "", err
	}
	cmd := fmt.Sprintf(
		"systemctl %scat %s --no-pager 2>/dev/null | head -1 | sed -e 's/#\\s*//g' || true",
		userOption,
		serverName,
	)
	cmdInfo, err := RunShellCmdWithRetry(
		ctx,
		SystemdBackOff,
		cmdUser,
		"GetSystemdUnitPath",
		cmd,
		logOut,
	)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(cmdInfo.StdOut.String()), nil
}

// IsProcessRunning checks if a process is running. This check is already in Ansible.
func IsProcessRunning(
	ctx context.Context,
	username, process string,
	logOut util.Buffer,
) (bool, error) {
	cmd := fmt.Sprintf("pgrep -lx %s 2>/dev/null || true", process)
	cmdInfo, err := RunShellCmd(ctx, username, "CheckRunningProcess", cmd, logOut)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to check if process is running: %s - %s", process, err.Error())
		logOut.WriteLine("Failed to check if process is running: %s - %s", process, err.Error())
		return false, err
	}
	stdOut := cmdInfo.StdOut.String()
	util.FileLogger().
		Infof(ctx, "Process %s state: %s", process, stdOut)
	logOut.WriteLine("Process %s state: %s", process, stdOut)
	return strings.Contains(stdOut, process), nil
}

func IsProcessEnabled(
	ctx context.Context,
	username, process string,
	logOut util.Buffer,
) (bool, error) {
	userOption, _, err := getUserOptionForUserLevel(ctx, username, process, logOut)
	if err != nil {
		return false, err
	}
	cmd := fmt.Sprintf("systemctl %sis-enabled %s 2>/dev/null || true", userOption, process)
	cmdInfo, err := RunShellCmdWithRetry(
		ctx,
		SystemdBackOff,
		username,
		"CheckEnabledProcess",
		cmd,
		logOut,
	)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to check if process is enabled: %s - %s", process, err.Error())
		logOut.WriteLine("Failed to check if process is enabled: %s - %s", process, err.Error())
		return false, err
	}
	stdOut := strings.TrimSpace(cmdInfo.StdOut.String())
	util.FileLogger().
		Infof(ctx, "Process %s enabled state: %s", process, stdOut)
	logOut.WriteLine("Process %s enabled state: %s", process, stdOut)
	return stdOut == "enabled", nil
}

// UpdateUserSystemdUnits updates the user systemd unit file for the given process only if it exists.
func UpdateUserSystemdUnits(
	ctx context.Context,
	username, process string,
	templateCtx map[string]any,
	logOut util.Buffer,
) error {
	fileInfo, ok := UserSystemdUnitsForUpdate[process]
	if !ok {
		util.FileLogger().Infof(ctx, "Skipping service file copy for process %s", process)
		logOut.WriteLine("Skipping service file copy for process %s", process)
		return nil
	}
	yes, path, err := IsUserSystemd(username, filepath.Base(fileInfo.Dest))
	if err != nil {
		util.FileLogger().
			Infof(ctx, "Failed to determine user systemd for %s - %v", process, err)
		logOut.WriteLine("Failed to determine user systemd for %s", process)
		return err
	}
	if !yes {
		util.FileLogger().Infof(ctx, "Skipping for non-user systemd for %s", process)
		logOut.WriteLine("Skipping for non-user systemd for %s", process)
		return nil
	}
	util.FileLogger().Infof(ctx, "Updating user systemd unit at %s", path)
	logOut.WriteLine("Updating user systemd unit at %s", path)
	_, err = CopyFile(
		ctx,
		templateCtx,
		filepath.Join(ServerTemplateSubpath, fileInfo.Src),
		path,
		fs.FileMode(0755),
		username,
	)
	if err != nil {
		return err
	}
	_, err = RunShellCmdWithRetry(
		ctx,
		SystemdBackOff,
		username,
		"ReloadSystemdDaemon",
		"systemctl --user daemon-reload",
		logOut,
	)
	return err
}
