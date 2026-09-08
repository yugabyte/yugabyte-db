// Copyright (c) YugabyteDB, Inc.

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

// EnabledSystemdUnits lists the systemd units to be enabled by default.
var EnabledSystemdUnits = []string{
	"yb-zip_purge_yb_logs.service",
	"yb-clean_cores.service",
	"yb-collect_metrics.service",
	"yb-zip_purge_yb_logs.timer",
	"yb-clean_cores.timer",
	"yb-collect_metrics.timer",
}

// ScriptFilesToCopy lists the files to be copied for server configuration.
var ScriptFilesToCopy = []struct {
	Src  string
	Dest string
}{
	{"yb-server-ctl.sh.j2", "bin/yb-server-ctl.sh"},
	{"clock-sync.sh.j2", "bin/clock-sync.sh"},
	{"clean_cores.sh.j2", "bin/clean_cores.sh"},
	{"zip_purge_yb_logs.sh.j2", "bin/zip_purge_yb_logs.sh"},
	{"collect_metrics_wrapper.sh.j2", "bin/collect_metrics_wrapper.sh"},
}

// CgroupScriptFilesToCopy lists additional scripts installed only when configure_cgroup is enabled.
var CgroupScriptFilesToCopy = []struct {
	Src  string
	Dest string
}{
	{"yb-tserver-cgroup-exec.sh.j2", "bin/yb-tserver-cgroup-exec.sh"},
	{"check_cpu_cgroup.sh.j2", "bin/check_cpu_cgroup.sh"},
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
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	// 2) determine yb_metric_dir
	yb_metrics_dir := filepath.Join(h.param.GetRemoteTmp(), "yugabyte/metrics")
	cmd := "systemctl show node_exporter | grep -oP '(?<=--collector.textfile.directory=)[^ ]+' | head -n1"
	h.logOut.WriteLine("Determining the node_exporter textfile directory")
	cmdInfo, err := module.RunShellCmdWithRetry(
		ctx,
		module.SystemdBackOff,
		h.username,
		"ShowSystemd",
		cmd,
		h.logOut,
	)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed in %v - %s", cmd, err.Error())
		return nil, err
	}
	if strings.TrimSpace(cmdInfo.StdOut.String()) != yb_metrics_dir {
		yb_metrics_dir = filepath.Join(h.param.GetYbHomeDir(), "metrics")
	}

	// 3) Execute the shell commands.
	err = h.execShellCommands(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	// 4) Setup the server scripts.
	err = h.setupServerScript(ctx, yb_metrics_dir)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	// 5) Enable the user systemd units.
	err = h.enableSystemdServices(ctx)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return nil, err
	}

	for _, process := range h.param.GetProcesses() {
		// 6) Configure the individual specified process.
		err = h.configureProcess(ctx, process)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
			return nil, err
		}
	}

	return nil, nil
}

func (h *ConfigureServerHandler) configureProcess(ctx context.Context, process string) error {
	mountPoints := h.param.GetMountPoints()
	if len(mountPoints) == 0 {
		return errors.New("mountPoints is required")
	}
	home := h.param.GetYbHomeDir()
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
				"rm -rf %s && ln -sf %s %s",
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

func (h *ConfigureServerHandler) enableSystemdServices(ctx context.Context) error {
	for _, unit := range EnabledSystemdUnits {
		err := module.EnableSystemdService(ctx, h.username, unit, h.logOut)
		if err != nil {
			util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
			return err
		}
		if strings.HasSuffix(unit, ".timer") {
			err := module.StartSystemdService(ctx, h.username, unit, h.logOut)
			if err != nil {
				util.FileLogger().
					Errorf(ctx, "Configure server failed - %s", err.Error())
				return err
			}
		}
	}
	enabled, err := module.IsUserSystemdEnabled(ctx, h.username, h.logOut)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Configure server failed - %s", err.Error())
		return err
	}
	if enabled {
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
		_, err = module.RunShellCmdWithRetry(
			ctx,
			module.SystemdBackOff,
			h.username,
			"LinkNetworkOnlineTarget",
			linkCmd,
			h.logOut,
		)
		if err != nil {
			util.FileLogger().
				Errorf(ctx, "Configure server failed in %v - %s", linkCmd, err.Error())
			return err
		}
	}

	return nil
}

func (h *ConfigureServerHandler) setupServerScript(
	ctx context.Context, yb_metrics_dir string,
) error {
	home := h.param.GetYbHomeDir()
	mountPoints := h.param.GetMountPoints()
	serverScriptContext := map[string]any{
		"mount_paths":        strings.Join(mountPoints, " "),
		"user_name":          h.username,
		"yb_cores_dir":       filepath.Join(home, "cores"),
		"systemd_option":     true,
		"yb_home_dir":        home,
		"num_cores_to_keep":  h.param.GetNumCoresToKeep(),
		"yb_metrics_dir":     yb_metrics_dir,
		"configure_cgroup":   h.param.GetConfigureCgroup(),
		"check_data_volumes": h.param.GetCheckDataVolumes(),
	}
	if h.param.AcceptableClockSkewWaitEnabled != nil {
		serverScriptContext["is_acceptable_clock_skew_wait_enabled"] =
			h.param.GetAcceptableClockSkewWaitEnabled()
	}
	if h.param.AcceptableClockSkewSec != nil {
		serverScriptContext["acceptable_clock_skew_sec"] = h.param.GetAcceptableClockSkewSec()
	}
	if h.param.AcceptableClockSkewMaxTries != nil {
		serverScriptContext["acceptable_clock_skew_max_tries"] =
			h.param.GetAcceptableClockSkewMaxTries()
	}

	for _, fileInfo := range ScriptFilesToCopy {
		_, err := module.CopyFile(
			ctx,
			serverScriptContext,
			filepath.Join(module.ServerTemplateSubpath, fileInfo.Src),
			filepath.Join(home, fileInfo.Dest),
			fs.FileMode(0755),
			h.username,
		)
		if err != nil {
			return err
		}
	}
	if h.param.GetConfigureCgroup() {
		for _, fileInfo := range CgroupScriptFilesToCopy {
			_, err := module.CopyFile(
				ctx,
				serverScriptContext,
				filepath.Join(module.ServerTemplateSubpath, fileInfo.Src),
				filepath.Join(home, fileInfo.Dest),
				fs.FileMode(0755),
				h.username,
			)
			if err != nil {
				return err
			}
		}
	}
	for _, process := range h.param.GetProcesses() {
		err := module.UpdateUserSystemdUnits(
			ctx,
			h.username,
			process,
			serverScriptContext,
			h.logOut,
		)
		if err != nil {
			return err
		}
	}
	return nil
}

func (h *ConfigureServerHandler) execShellCommands(
	ctx context.Context) error {
	mountPoints := h.param.GetMountPoints()
	if len(mountPoints) == 0 {
		return errors.New("mountPoints is required")
	}
	home := h.param.GetYbHomeDir()
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
				"rm -rf %s && ln -sf %s %s",
				filepath.Join(home, "cores"),
				filepath.Join(mountPoint, "cores"),
				filepath.Join(home, "cores"),
			),
		},
		// The health check writes its log here. Only logs/ goes to the data partition -
		// metrics/ itself holds the node_exporter textfiles the collector rewrites each run,
		// and they belong where node_exporter is configured to look.
		{"make-metrics-logs-dir", fmt.Sprintf(
			"mkdir -p %s && chmod 0755 %s",
			filepath.Join(mountPoint, "metrics/logs"),
			filepath.Join(mountPoint, "metrics/logs"),
		)},
		{"make-yb-metrics-dir", fmt.Sprintf("mkdir -p %s", filepath.Join(home, "metrics"))},
		// Not a plain "rm -rf && ln -sf": a YBA upgrade puts the health check script on nodes
		// before their next configure and it logs here from its first run, so a real directory
		// found here holds logs someone reading a support bundle wants - move them across
		// rather than delete them. ln -sfn, or a second run links inside the first link.
		{"symlink-metrics-logs", fmt.Sprintf(
			"if [ ! -L %s ] && [ -d %s ]; then "+
				"cp -a %s/. %s/ 2>/dev/null || true; rm -rf %s; fi; "+
				"ln -sfn %s %s",
			filepath.Join(home, "metrics/logs"),
			filepath.Join(home, "metrics/logs"),
			filepath.Join(home, "metrics/logs"),
			filepath.Join(mountPoint, "metrics/logs"),
			filepath.Join(home, "metrics/logs"),
			filepath.Join(mountPoint, "metrics/logs"),
			filepath.Join(home, "metrics/logs"),
		)},
	}
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}
