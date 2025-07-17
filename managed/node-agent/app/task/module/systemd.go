// Copyright (c) YugaByte, Inc.

package module

import (
	"context"
	"errors"
	"fmt"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
)

const (
	UserSystemdUnitPath = ".config/systemd/user"
)

func IsUserSystemd(username, serverName string) (bool, error) {
	info, err := util.UserInfo(username)
	if err != nil {
		return false, err
	}
	if !strings.HasSuffix(serverName, ".service") && !strings.HasSuffix(serverName, ".timer") {
		serverName = serverName + ".service"
	}
	path := filepath.Join(info.User.HomeDir, ".config/systemd/user", serverName)
	_, err = os.Stat(path)
	if err == nil {
		return true, nil
	}
	if errors.Is(err, os.ErrNotExist) {
		return false, nil
	}
	return false, err
}

func getUserOptionForUserLevel(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) (string, string, error) {
	userOption := ""
	cmdUser := ""
	if username != "" {
		yes, err := IsUserSystemd(username, serverName)
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
	cmd := fmt.Sprintf(
		"systemctl %sdaemon-reload && systemctl %senable %s",
		userOption,
		userOption,
		serverName,
	)
	util.FileLogger().Infof(ctx, "Running enable systemd unit command: %s", cmd)
	logOut.WriteLine("Running enable systemd unit command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "EnableSystemdUnit", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command: %s - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return err
	}
	return nil
}

func StartSystemdService(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
		logOut.WriteLine("Failed to get user option for systemd: %s - %s", serverName, err.Error())
		return err
	}
	cmd := fmt.Sprintf("systemctl %sstart %s", userOption, serverName)
	util.FileLogger().Infof(ctx, "Running systemd start command: %s", cmd)
	logOut.WriteLine("Running systemd start command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "StartSystemdService", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command: %s - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return err
	}
	return nil
}

func StopSystemdService(
	ctx context.Context,
	username, serverName string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
		logOut.WriteLine("Failed to get user option for systemd: %s - %s", serverName, err.Error())
		return err
	}
	cmd := fmt.Sprintf("systemctl %s stop %s", userOption, serverName)
	util.FileLogger().Infof(ctx, "Running systemd stop command: %s", cmd)
	logOut.WriteLine("Running systemd stop command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "StopSystemdService", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command: %s - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return err
	}
	return nil
}

func DisableSystemdService(
	ctx context.Context,
	username, serverName, unitPathToBeRemoved string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
		logOut.WriteLine("Failed to get user option for systemd: %s - %s", serverName, err.Error())
		return err
	}
	cmd := fmt.Sprintf(
		"systemctl %sdaemon-reload && systemctl %sstop %s && systemctl %sdisable %s",
		userOption,
		userOption,
		serverName,
		userOption,
		serverName,
	)
	util.FileLogger().Infof(ctx, "Running systemd disable command: %s", cmd)
	logOut.WriteLine("Running systemd disable command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "DisableSystemdService", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command: %s - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
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
	cmd = fmt.Sprintf("systemctl %sdaemon-reload", userOption)
	util.FileLogger().Infof(ctx, "Running systemd daemon-reload command: %s", cmd)
	logOut.WriteLine("Running systemd daemon-reload command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "DisableSystemdService", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command %v - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return err
	}
	return nil
}

func ControlSystemdService(
	ctx context.Context,
	username, serverName, controlType string,
	logOut util.Buffer,
) error {
	userOption, cmdUser, err := getUserOptionForUserLevel(ctx, username, serverName, logOut)
	if err != nil {
		util.FileLogger().
			Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
		logOut.WriteLine("Failed to get user option for systemd: %s - %s", serverName, err.Error())
		return err
	}
	cmd := fmt.Sprintf(
		"systemctl %sdaemon-reload && systemctl %senable %s && systemctl %s%s %s",
		userOption,
		userOption,
		serverName,
		userOption,
		controlType,
		serverName,
	)
	util.FileLogger().Infof(ctx, "Running systemd control command: %s", cmd)
	logOut.WriteLine("Running systemd control command: %s", cmd)
	if _, err := RunShellCmd(ctx, cmdUser, "ControlSystemdService", cmd, logOut); err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command %v - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return err
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
		util.FileLogger().
			Errorf(ctx, "Failed to get user option for systemd: %s - %s", serverName, err.Error())
		logOut.WriteLine("Failed to get user option for systemd: %s - %s", serverName, err.Error())
		return "", err
	}
	cmd := fmt.Sprintf(
		"systemctl %scat %s 2>/dev/null | head -1 | sed -e 's/#\\s*//g' || true",
		userOption,
		serverName,
	)
	util.FileLogger().Infof(ctx, "Running systemd cat command: %s", cmd)
	logOut.WriteLine("Running systemd cat command: %s", cmd)
	info, err := RunShellCmd(ctx, cmdUser, "SystemdUnitPath", cmd, logOut)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Failed to run systemd command %s - %s", cmd, err.Error())
		logOut.WriteLine("Failed to run systemd command: %s - %s", cmd, err.Error())
		return "", err
	}
	return strings.TrimSpace(info.StdOut.String()), nil
}
