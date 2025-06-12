// Copyright (c) YugaByte, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/app/task/helpers"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"strings"
)

const (
	SystemdUnitPath       = ".config/systemd/user"
	ServerTemplateSubpath = "server/"
)

var SystemdUnits = []string{
	"yb-zip_purge_yb_logs.service",
	"yb-clean_cores.service",
	"yb-collect_metrics.service",
	"yb-zip_purge_yb_logs.timer",
	"yb-clean_cores.timer",
	"yb-collect_metrics.timer",
}

type ConfigureServerHandler struct {
	shellTask *ShellTask
	param     *pb.ConfigureServerInput
	username  string
	logOut    util.Buffer
}

func NewConfigureServerHandler(
	param *pb.ConfigureServerInput,
	username string,
) *ConfigureServerHandler {
	return &ConfigureServerHandler{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *ConfigureServerHandler) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *ConfigureServerHandler) String() string {
	return "Configure Server Task"
}

func (h *ConfigureServerHandler) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Info(ctx, "Starting configure server handler.")

	// 0. Validate that the processes are specified.
	if len(h.param.GetProcesses()) == 0 {
		err := errors.New("processes is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 1) figure out home dir
	home := ""
	if h.param.GetYbHomeDir() != "" {
		home = h.param.GetYbHomeDir()
	} else {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 2) determine yb_metric_dir
	yb_metrics_dir := filepath.Join(h.param.GetRemoteTmp(), "yugabyte/metrics")
	cmd := "systemctl show node_exporter | grep -oP '(?<=--collector.textfile.directory=)[^ ]+' | head -n1"
	h.logOut.WriteLine("Determing the node_exporter textfile directory")
	cmdInfo, err := module.RunShellCmd(ctx, h.username, h.String(), cmd, h.logOut)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed in %v - %s", cmd, err.Error())
		return nil, err
	}
	if cmdInfo.StdOut.String() != yb_metrics_dir {
		yb_metrics_dir = filepath.Join(home, "metrics")
	}

	// 3) Execute the shell commands.
	err = h.execShellCommands(ctx, home)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	// 4) Setup the server scripts.
	err = h.setupServerScript(ctx, home, yb_metrics_dir)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	// 5) Enable the user systemd units.
	err = h.enableSystemdServices(ctx, home)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	for _, process := range h.param.GetProcesses() {
		// 6) Configure the individual specified process.
		err = h.configureProcess(ctx, home, process)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
			return nil, err
		}
	}

	return nil, nil
}

func (h *ConfigureServerHandler) configureProcess(ctx context.Context, home, process string) error {
	mountPoints := h.param.GetMountPoints()
	if len(mountPoints) == 0 {
		return errors.New("mountPoints is required")
	}
	mountPoint := mountPoints[0]
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{
			fmt.Sprintf("make-yb-%s-conf-dir", process),
			fmt.Sprintf("mkdir -p %s", filepath.Join(home, process, "conf")),
		},
		{
			"create-mount-logs-directory",
			fmt.Sprintf("mkdir -p %s", filepath.Join(mountPoint, "yb-data/", process, "logs")),
		},
		{
			"symlink-logs-to-yb-logs",
			fmt.Sprintf(
				"unlink %s > /dev/null 2>&1; ln -sf %s %s",
				filepath.Join(home, process, "logs"),
				filepath.Join(mountPoint, "yb-data/", process, "logs"),
				filepath.Join(home, process, "logs"),
			),
		},
	}

	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}

func (h *ConfigureServerHandler) enableSystemdServices(ctx context.Context, home string) error {
	for _, unit := range SystemdUnits {
		cmd := module.EnableSystemdUnit(h.username, unit)
		h.logOut.WriteLine("Running configure server phase: %s", cmd)
		util.FileLogger().Infof(ctx, "Running command %v", cmd)
		_, err := module.RunShellCmd(ctx, h.username, h.String(), cmd, h.logOut)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Configure server failed in %v - %s", cmd, err.Error())
			return err
		}

		if unit != "network-online.target" && unit[len(unit)-6:] == "timer" {
			startCmd := module.StartSystemdUnit(h.username, unit)
			h.logOut.WriteLine("Running configure server phase: %s", startCmd)
			util.FileLogger().Infof(ctx, "Running command %v", startCmd)
			_, err = module.RunShellCmd(ctx, h.username, h.String(), startCmd, h.logOut)
			if err != nil {
				util.FileLogger().
					Errorf(ctx, "Configure server failed in %v - %s", cmd, err.Error())
				return err
			}
		}
	}

	info, err := helpers.GetOSInfo()
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error retreiving OS information %s", err.Error())
		return err
	}

	unitDir := "/lib/systemd/system"
	if strings.Contains(info.ID, "suse") || strings.Contains(info.Family, "suse") {
		unitDir = "/usr/lib/systemd/system"
	}

	// Link network-online.target if required
	linkCmd := fmt.Sprintf("systemctl --user link %s/network-online.target", unitDir)
	h.logOut.WriteLine("Running configure server phase: %s", linkCmd)
	util.FileLogger().Infof(ctx, "Running command %v", linkCmd)
	_, err = module.RunShellCmd(ctx, h.username, h.String(), linkCmd, h.logOut)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed in %v - %s", linkCmd, err.Error())
		return err
	}
	return nil
}

func (h *ConfigureServerHandler) setupServerScript(
	ctx context.Context,
	home, yb_metrics_dir string,
) error {
	serverScriptContext := map[string]any{
		"mount_paths":       strings.Join(h.param.GetMountPoints(), " "),
		"user_name":         h.username,
		"yb_cores_dir":      filepath.Join(home, "cores"),
		"systemd_option":    true,
		"yb_home_dir":       home,
		"num_cores_to_keep": h.param.GetNumCoresToKeep(),
		"yb_metrics_dir":    yb_metrics_dir,
	}

	// Copy yb-server-ctl.sh script.
	err := module.CopyFile(
		ctx,
		serverScriptContext,
		filepath.Join(ServerTemplateSubpath, "yb-server-ctl.sh.j2"),
		filepath.Join(home, "bin", "yb-server-ctl.sh"),
		fs.FileMode(0755),
		h.username,
	)
	if err != nil {
		return err
	}

	// Copy clock-sync.sh script.
	err = module.CopyFile(
		ctx,
		serverScriptContext,
		filepath.Join(ServerTemplateSubpath, "clock-sync.sh.j2"),
		filepath.Join(home, "bin", "clock-sync.sh"),
		fs.FileMode(0755),
		h.username,
	)
	if err != nil {
		return err
	}

	// Copy clean_cores.sh script.
	err = module.CopyFile(
		ctx,
		serverScriptContext,
		filepath.Join(ServerTemplateSubpath, "clean_cores.sh.j2"),
		filepath.Join(home, "bin", "clean_cores.sh"),
		fs.FileMode(0755),
		h.username,
	)
	if err != nil {
		return err
	}

	// Copy zip_purge_yb_logs.sh.sh script.
	err = module.CopyFile(
		ctx,
		serverScriptContext,
		filepath.Join(ServerTemplateSubpath, "zip_purge_yb_logs.sh.j2"),
		filepath.Join(home, "bin", "zip_purge_yb_logs.sh"),
		fs.FileMode(0755),
		h.username,
	)
	if err != nil {
		return err
	}

	// Copy collect_metrics_wrapper.sh script.
	err = module.CopyFile(
		ctx,
		serverScriptContext,
		filepath.Join(ServerTemplateSubpath, "collect_metrics_wrapper.sh.j2"),
		filepath.Join(home, "bin", "collect_metrics_wrapper.sh"),
		fs.FileMode(0755),
		h.username,
	)
	if err != nil {
		return err
	}

	return nil
}

func (h *ConfigureServerHandler) execShellCommands(
	ctx context.Context,
	home string,
) error {
	mountPoints := h.param.GetMountPoints()
	if len(mountPoints) == 0 {
		return errors.New("mountPoints is required")
	}
	mountPoint := mountPoints[0]
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{"make-yb-bin-dir", fmt.Sprintf("mkdir -p %s", filepath.Join(home, "bin"))},
		{"make-cores-dir", fmt.Sprintf("mkdir -p %s", filepath.Join(mountPoint, "cores"))},
		{
			"symlink-cores-to-yb-cores",
			fmt.Sprintf(
				"unlink %s > /dev/null 2>&1; ln -sf %s %s",
				filepath.Join(home, "cores"),
				filepath.Join(mountPoint, "cores"),
				filepath.Join(home, "cores"),
			),
		},
	}
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}
