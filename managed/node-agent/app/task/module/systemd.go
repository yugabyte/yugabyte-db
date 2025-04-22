// Copyright (c) YugaByte, Inc.

package module

import (
	"errors"
	"fmt"
	"node-agent/util"
	"os"
	"path/filepath"
	"strings"
)

func IsUserSystemd(username, serverName string) (bool, error) {
	info, err := util.UserInfo(username)
	if err != nil {
		return false, err
	}
	if !strings.HasSuffix(serverName, ".service") {
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

func ControlServerCmd(username, serverName, controlType string) (string, error) {
	userOption := ""
	if username != "" {
		yes, err := IsUserSystemd(username, serverName)
		if err != nil {
			return "", err
		}
		if yes {
			userOption = "--user "
		}
	}
	return fmt.Sprintf(
		"systemctl %sdaemon-reload && systemctl %senable %s && systemctl %s%s %s",
		userOption,
		userOption,
		serverName,
		userOption,
		controlType,
		serverName,
	), nil
}
