// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

const OtelCollectorService = "otel-collector.service"

const otelCollectorRootUnitName = "otel-collector"

// A crash-looping collector can live well under 1 s in each RestartSec cycle, so poll often.
const otelCollectorRunningPollInterval = 200 * time.Millisecond

const (
	// Name of the executable inside the published archive. The YugabyteDB unified collector is a
	// custom ocb distribution rather than the upstream contrib one, so it does not ship under the
	// contrib name.
	otelCollectorPackagedBinary = "otelcol-unified"
	// Name the executable is installed under on the node. This becomes the running process name,
	// which the systemd unit, the health check's `ps -C` lookup and the `process` label on the
	// per-process metrics all key off, so it must stay stable across collector version changes.
	otelCollectorInstalledBinary = "otelcol-contrib"
)

type InstallOtelCollector struct {
	shellTask *ShellTask
	param     *pb.InstallOtelCollectorInput
	username  string
	logOut    util.Buffer
	// Where a root-level unit lives, on nodes provisioned with root systemd.
	rootUnitPath string
	// stopRootUnit and refreshPurge are tryStopRootOtelCollector and refreshLogPurgeScript;
	// tests replace them.
	stopRootUnit func(context.Context) rootOtelCollectorStop
	refreshPurge func(context.Context, string) error
}

func NewInstallOtelCollectorHandler(
	param *pb.InstallOtelCollectorInput,
	username string,
) *InstallOtelCollector {
	h := &InstallOtelCollector{
		param:        param,
		username:     username,
		logOut:       util.NewBuffer(module.MaxBufferCapacity),
		rootUnitPath: filepath.Join(systemSystemdUnitDir, OtelCollectorService),
	}
	h.stopRootUnit = h.tryStopRootOtelCollector
	h.refreshPurge = h.refreshLogPurgeScript
	return h
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *InstallOtelCollector) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *InstallOtelCollector) String() string {
	return "Install otel collector Task"
}

func (h *InstallOtelCollector) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	util.FileLogger().Infof(ctx, "Starting otel collector installation")

	// 1) figure out home dir
	if h.param.GetYbHomeDir() == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// Fast path: caller only wants to refresh the on-node log-purge script and
	// its audit-log cleanup env. This is invoked on every audit-log settings
	// change (even for universes that don't have otel-collector installed) so
	// that fixes shipped in newer node-agent builds reach the on-node script.
	if h.param.GetRefreshScriptOnly() {
		util.FileLogger().Infof(
			ctx, "Refresh-only mode: skipping otel-collector install steps")
		if err := h.writeLogCleanupEnv(ctx, h.param.GetYbHomeDir()); err != nil {
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
		if err := h.refreshPurge(ctx, h.param.GetYbHomeDir()); err != nil {
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
		return nil, nil
	}

	if h.param.GetDisableCollector() {
		err := h.disableOtelCollector(ctx, h.param.GetYbHomeDir())
		if err == nil {
			err = h.refreshPurge(ctx, h.param.GetYbHomeDir())
		}
		if err != nil {
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
		return nil, nil
	}

	userInfo, err := util.UserInfo(h.username)
	if err != nil {
		return nil, err
	}
	ybUserHome := userInfo.User.HomeDir
	// 2) Put & setup the otel collector.
	err = h.execOtelCollectorSetupSteps(ctx, h.param.GetYbHomeDir())
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// Runs ahead of the stop in 4) and the config swap in 5) so a rejected config leaves the
	// running collector and its existing config.yml untouched.
	if h.param.GetOtelColConfigFile() != "" {
		if err = h.validateOtelCollectorConfig(ctx, h.param.GetYbHomeDir()); err != nil {
			util.FileLogger().Error(ctx, err.Error())
			return nil, err
		}
	}

	// 3) Place the otel-collector.service at desired location.
	otelColMaxMemory := h.param.GetOtelColMaxMemory()
	if otelColMaxMemory == 0 {
		otelColMaxMemory = 2048 // Default to 2048MB if not specified
	}
	otelCollectorServiceContext := map[string]any{
		"user_name":           h.username,
		"yb_home_dir":         h.param.GetYbHomeDir(),
		"otel_col_max_memory": otelColMaxMemory,
		// Must mirror the conditions the setup steps use to write these credentials.
		"otel_col_aws_creds_present": h.param.GetOtelColAwsAccessKey() != "" &&
			h.param.GetOtelColAwsSecretKey() != "",
		"otel_col_gcp_creds_present": h.param.GetOtelColGcpCredsFile() != "",
	}

	// Copy otel-collector.service
	_, err = module.CopyFile(
		ctx,
		otelCollectorServiceContext,
		filepath.Join(module.ServerTemplateSubpath, OtelCollectorService),
		filepath.Join(ybUserHome, module.UserSystemdUnitPath, OtelCollectorService),
		fs.FileMode(0755),
		h.username,
	)

	if err != nil {
		return nil, err
	}

	// 4) Stop and disable the systemd service. A root unit, if any, holds the ports and the queue
	// that the user collector needs, so it stops first, but only when the user manager runs. With
	// no user manager the user steps fail, so the root collector is left to export, as before.
	if hasRoot, err := h.hasRootOtelCollectorUnit(); err != nil || hasRoot {
		managerRuns, err := module.IsUserSystemdEnabled(ctx, h.username, nil)
		if err != nil {
			msg := fmt.Sprintf("Root-level %s not stopped: the user systemd manager check"+
				" failed - %s", otelCollectorRootUnitName, err)
			util.FileLogger().Warnf(ctx, "%s", msg)
			h.logOut.WriteLine("%s", msg)
		} else if managerRuns {
			h.stopRootOtelCollector(ctx)
		}
	}
	if err := module.DisableSystemdService(
		ctx,
		h.username,
		OtelCollectorService,
		"", // Don't remove the unit file - we need it for re-enabling later
		h.logOut); err != nil {
		return nil, err
	}

	// 5) Configure the otel-collector service.
	err = h.configureOtelCollector(ctx, h.param.GetYbHomeDir())
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 5b) Refresh the log-purge script on disk so that any updates to
	// zip_purge_yb_logs.sh.j2 shipped with newer node-agent builds (e.g. new
	// log_cleanup_env variables consumed by the script) are picked up on every
	// audit-config change without requiring a separate ConfigureServer run.
	err = h.refreshPurge(ctx, h.param.GetYbHomeDir())
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}

	// 6) Start and enable the service only if config file exists
	if h.param.GetOtelColConfigFile() != "" {
		if err = module.StartSystemdService(ctx, h.username, OtelCollectorService, h.logOut); err != nil {
			return nil, err
		}
		if err = module.EnableSystemdService(ctx, h.username, OtelCollectorService, h.logOut); err != nil {
			return nil, err
		}
		// start/enable both return 0 over a collector that died parsing its config.
		if err = module.VerifySystemdServiceStarted(
			ctx, h.username, OtelCollectorService, h.logOut,
		); err != nil {
			return nil, err
		}
	}
	return nil, nil
}

// Covers config unmarshalling and Validate() only - components are not constructed until the
// service runs, so an exporter that fails to build still has to be caught after start.
func (h *InstallOtelCollector) validateOtelCollectorConfig(
	ctx context.Context,
	ybHome string,
) error {
	cmd := fmt.Sprintf(
		"%s validate --config=file:%s",
		filepath.Join(ybHome, "otel-collector", otelCollectorInstalledBinary),
		h.param.GetOtelColConfigFile(),
	)
	if _, err := module.RunShellCmd(
		ctx, h.username, "ValidateOtelCollectorConfig", cmd, h.logOut); err != nil {
		return fmt.Errorf("otel-collector config was rejected: %w", err)
	}
	return nil
}

// GetOtelCollectorSetupSteps returns the sequence of steps needed for configuring the otel collector.
func (h *InstallOtelCollector) execOtelCollectorSetupSteps(
	ctx context.Context,
	ybHome string,
) error {
	pkgName := filepath.Base(h.param.GetOtelColPackagePath())
	otelCollectorPackagePath := filepath.Join(h.param.GetRemoteTmp(), pkgName)
	otelCollectorDirectory := filepath.Join(ybHome, "otel-collector")
	mountPoint := ""
	if len(h.param.GetMountPoints()) > 0 {
		mountPoint = h.param.GetMountPoints()[0]
	}

	steps := []struct {
		Desc string
		Cmd  string
	}{
		ensureOtelCollectorDirStep(ybHome),
		{
			"untar-otel-collector",
			fmt.Sprintf(
				"tar --no-same-owner -xzvf %s -C %s",
				otelCollectorPackagePath,
				otelCollectorDirectory,
			),
		},
		{
			// mv rather than cp: rename(2) succeeds even when the previous binary is still mapped
			// by a running collector, which overwriting in place would not (ETXTBSY).
			"rename-otel-collector-binary",
			fmt.Sprintf(
				"mv -f %s %s",
				filepath.Join(otelCollectorDirectory, otelCollectorPackagedBinary),
				filepath.Join(otelCollectorDirectory, otelCollectorInstalledBinary),
			),
		},
		{
			fmt.Sprintf("ensure 755 permission for %s", otelCollectorInstalledBinary),
			fmt.Sprintf(
				"chmod -R 755 %s",
				filepath.Join(otelCollectorDirectory, otelCollectorInstalledBinary),
			),
		},
		{
			"create OpenTelemetry collector logs directory",
			fmt.Sprintf(
				"mkdir -p %s && chmod 0755 %s",
				filepath.Join(mountPoint, "otel-collector/logs"),
				filepath.Join(mountPoint, "otel-collector/logs"),
			),
		},
		{
			"symlink OpenTelemetry collector logs directory",
			fmt.Sprintf(
				"rm -rf %s && ln -sf %s %s && chmod 0755 %s",
				filepath.Join(ybHome, "otel-collector/logs"),
				filepath.Join(mountPoint, "otel-collector/logs"),
				filepath.Join(ybHome, "otel-collector/logs"),
				filepath.Join(ybHome, "otel-collector/logs"),
			),
		},
		{
			"create OpenTelemetry collector persistent queues directory",
			fmt.Sprintf(
				"mkdir -p %s && chmod 0755 %s",
				filepath.Join(mountPoint, "otel-collector/queue"),
				filepath.Join(mountPoint, "otel-collector/queue"),
			),
		},
		{
			"symlink OpenTelemetry collector persistent queues directory",
			fmt.Sprintf(
				"rm -rf %s && ln -sf %s %s && chmod 0755 %s",
				filepath.Join(ybHome, "otel-collector/queue"),
				filepath.Join(mountPoint, "otel-collector/queue"),
				filepath.Join(ybHome, "otel-collector/queue"),
				filepath.Join(ybHome, "otel-collector/queue"),
			),
		},
		{
			"delete-otel-collector-package",
			fmt.Sprintf("rm -rf %s", otelCollectorPackagePath),
		},
	}

	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}

func (h *InstallOtelCollector) configureOtelCollector(ctx context.Context, ybHome string) error {
	// The shell paths are quoted, so a yb_home with a space or a quote stays one argument.
	otelCollectorConfigFile := shellSingleQuote(
		filepath.Join(ybHome, "otel-collector", "config.yml"),
	)
	otelColLogCleanupEnv := filepath.Join(ybHome, "otel-collector", "log_cleanup_env")
	awsCredsFile := shellSingleQuote(filepath.Join(ybHome, ".aws", "credentials"))
	gcpCredsFile := shellSingleQuote(filepath.Join(ybHome, "otel-collector", "gcp_creds"))

	steps := []struct {
		Desc string
		Cmd  string
	}{
		{
			"remove-otel-collector-config-file-if-exists",
			fmt.Sprintf(
				"rm -rf %s",
				otelCollectorConfigFile,
			),
		},
		{
			"create-aws-creds-dir",
			fmt.Sprintf("mkdir -p %s", shellSingleQuote(filepath.Join(ybHome, ".aws"))),
		},
		{
			"remove-otel-collector-aws-block-if-exists",
			fmt.Sprintf(`if [ -f %s ]; then \
		awk '/# BEGIN YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS/ {inblock=1} \
		/# END YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS/ {inblock=0; next} \
		!inblock' %s > %s.tmp && mv %s.tmp %s; fi`,
				awsCredsFile,
				awsCredsFile,
				awsCredsFile,
				awsCredsFile,
				awsCredsFile,
			),
		},
		{
			"remove-gcp-credentials",
			fmt.Sprintf("rm -rf %s", gcpCredsFile),
		},
	}
	// log_cleanup_env carries the audit-log retention settings that the
	// zip_purge_yb_logs.sh script sources on every run. Regenerating it here
	// keeps the full install flow in sync with the refresh-only flow.
	steps = append(steps, logCleanupEnvSteps(otelColLogCleanupEnv,
		h.param.GetYcqlAuditLogLevel(),
		h.param.GetYsqlAuditLogRetentionDays(),
		h.param.GetYcqlAuditLogRetentionDays(),
		h.param.GetYsqlAuditLineStartRegex())...)

	if h.param.GetOtelColAwsAccessKey() != "" && h.param.GetOtelColAwsSecretKey() != "" {
		steps = append(steps, struct {
			Desc string
			Cmd  string
		}{
			"append-otel-collector-creds",
			fmt.Sprintf(
				`echo '# BEGIN YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS
			[otel-collector]
			aws_access_key_id = %s
			aws_secret_access_key = %s
			# END YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS' >> %s && chmod 440 %s`,
				h.param.GetOtelColAwsAccessKey(),
				h.param.GetOtelColAwsSecretKey(),
				awsCredsFile,
				awsCredsFile,
			),
		})
	}

	if h.param.GetOtelColConfigFile() != "" {
		steps = append(steps, struct {
			Desc string
			Cmd  string
		}{
			"place-new-otel-collector-config-file",
			fmt.Sprintf(
				"mv %s %s",
				shellSingleQuote(h.param.GetOtelColConfigFile()),
				otelCollectorConfigFile,
			),
		})
	}

	if h.param.GetOtelColGcpCredsFile() != "" {
		steps = append(steps, struct {
			Desc string
			Cmd  string
		}{
			"place-new-gcp-creds-file",
			fmt.Sprintf(
				"mv %s %s",
				shellSingleQuote(h.param.GetOtelColGcpCredsFile()),
				gcpCredsFile,
			),
		})
	}

	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	return nil
}

// userOtelCollectorNeedsStop reports whether the disable path must stop the user unit. Earlier
// installs wrote it also on root-systemd nodes, where systemctl --user can fail.
func (h *InstallOtelCollector) userOtelCollectorNeedsStop(ctx context.Context) (bool, error) {
	hasUserUnit, _, err := module.IsUserSystemd(h.username, OtelCollectorService)
	if err != nil || !hasUserUnit {
		return false, err
	}
	managerRuns, err := module.IsUserSystemdEnabled(ctx, h.username, h.logOut)
	if err != nil || managerRuns {
		return managerRuns, err
	}
	return false, h.disableUserOtelCollectorWithoutManager(ctx)
}

// disableUserOtelCollectorWithoutManager runs when the manager probe fails. The probe can be
// wrong, so it tries one stop first, then disables the unit by removing its link.
func (h *InstallOtelCollector) disableUserOtelCollectorWithoutManager(ctx context.Context) error {
	steps := []struct {
		Desc string
		Cmd  string
	}{
		{"StopOtelCollector", "systemctl --user stop " + OtelCollectorService},
		{"DisableOtelCollector", "systemctl --user disable " + OtelCollectorService},
	}
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err == nil {
		return nil
	}
	info, err := util.UserInfo(h.username)
	if err != nil {
		return err
	}
	if err := h.removeOtelCollectorWantsLink(ctx, info.User.HomeDir); err != nil {
		return err
	}
	binary := otelCollectorUnitBinary(h.param.GetYbHomeDir())
	// The unit restarts every RestartSec, so one check can miss a crash-looping collector.
	running, err := otelCollectorRunsWithin(
		ctx,
		module.SystemdSettleInterval,
		otelCollectorRunningPollInterval,
		func() (bool, error) {
			// No task output: the poll runs about 75 times and would crowd it out.
			cmdInfo, err := module.RunShellCmd(
				ctx,
				h.username,
				"CheckOtelCollectorProcess",
				otelCollectorProcessCheckCmd(binary, "/proc"),
				nil,
			)
			if err != nil {
				return false, fmt.Errorf("check for a running %s: %w", binary, err)
			}
			return strings.TrimSpace(cmdInfo.StdOut.String()) != "", nil
		},
	)
	if err != nil {
		return err
	}
	if running {
		return otelCollectorStillRunningError(binary)
	}
	msg := "No user systemd manager: otel-collector unit disabled by removing its link"
	util.FileLogger().Warnf(ctx, "%s", msg)
	h.logOut.WriteLine("%s", msg)
	return nil
}

// removeOtelCollectorWantsLink does what systemctl --user disable does for this unit, whose only
// install target is default.target. The unit file stays for a later enable.
func (h *InstallOtelCollector) removeOtelCollectorWantsLink(
	ctx context.Context,
	homeDir string,
) error {
	link := filepath.Join(
		homeDir,
		".config/systemd/user/default.target.wants",
		OtelCollectorService,
	)
	_, err := module.RunShellCmd(
		ctx,
		h.username,
		"RemoveOtelCollectorWantsLink",
		"rm -f "+shellSingleQuote(link),
		h.logOut,
	)
	return err
}

// otelCollectorUnitBinary returns the binary path as the unit's ExecStart writes it.
// filepath.Join would clean it, so pgrep would miss a yb_home_dir that ends in "/".
func otelCollectorUnitBinary(ybHome string) string {
	return ybHome + "/otel-collector/" + otelCollectorInstalledBinary
}

// otelCollectorRootCgroup is the cgroup of a process that the root unit runs.
const otelCollectorRootCgroup = "/system.slice/" + OtelCollectorService

// otelCollectorProcessCheckCmd prints the PIDs of collectors that run binary. It skips the root
// unit's, which gets its own warning, and those that exited after pgrep (grep exits 2). pgrep
// exits 1 when nothing matches; a higher exit is a pgrep error and must fail.
func otelCollectorProcessCheckCmd(binary, procDir string) string {
	pattern := shellSingleQuote("^" + regexp.QuoteMeta(binary) + "( |$)")
	proc := shellSingleQuote(procDir) + `/"$p"`
	return fmt.Sprintf(
		`pids=$(pgrep -f %s); rc=$?; [ "$rc" -le 1 ] || exit "$rc"; `+
			`for p in $pids; do grep -qsF %s %s/cgroup; [ $? -ne 1 ] || echo "$p"; done; exit 0`,
		pattern,
		shellSingleQuote(otelCollectorRootCgroup),
		proc,
	)
}

// otelCollectorRunsWithin calls isRunning until it reports true or the window ends.
func otelCollectorRunsWithin(
	ctx context.Context,
	window, interval time.Duration,
	isRunning func() (bool, error),
) (bool, error) {
	deadline := time.Now().Add(window)
	for {
		running, err := isRunning()
		if err != nil || running {
			return running, err
		}
		if time.Now().Add(interval).After(deadline) {
			return false, nil
		}
		select {
		case <-ctx.Done():
			return false, ctx.Err()
		case <-time.After(interval):
		}
	}
}

// rootOtelCollectorStop is the outcome of the root unit stop.
type rootOtelCollectorStop int

const (
	rootOtelCollectorNotRunning rootOtelCollectorStop = iota // Stopped, or not running.
	rootOtelCollectorStateUnknown
	rootOtelCollectorStopFailed // The root unit still runs or starts at boot.
)

// otelCollectorStillRunningError reports a user collector that runs on with no manager to stop it.
// The check skips the root unit's collector, which gets its own warning.
func otelCollectorStillRunningError(binary string) error {
	return fmt.Errorf(
		"%s is still running and cannot be stopped: the user systemd manager does not respond",
		binary,
	)
}

func (h *InstallOtelCollector) hasRootOtelCollectorUnit() (bool, error) {
	if _, err := os.Stat(h.rootUnitPath); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return false, nil
		}
		return false, err
	}
	return true, nil
}

// stopRootOtelCollector stops a root-level collector unit. Earlier installs wrote the user unit
// also on root-systemd nodes, so a node can have both, and node agent only runs the user unit.
// A failure does not fail the task: gflags upgrades and node operations run this too, and they
// passed with both collectors before. The task output says that export can continue.
func (h *InstallOtelCollector) stopRootOtelCollector(ctx context.Context) {
	msg := ""
	switch h.stopRootUnit(ctx) {
	case rootOtelCollectorStopFailed:
		msg = fmt.Sprintf("The root-level %s could not be stopped or disabled, so it can keep"+
			" running and exporting, or start at boot. Check its sudoers rules and the node-agent"+
			" log.", otelCollectorRootUnitName)
	case rootOtelCollectorStateUnknown:
		msg = fmt.Sprintf("The state of the root-level %s could not be read. If it runs, it can"+
			" keep running and exporting.", otelCollectorRootUnitName)
	}
	if msg != "" {
		util.FileLogger().Warnf(ctx, "%s", msg)
		h.logOut.WriteLine("%s", msg)
	}
}

// tryStopRootOtelCollector stops and disables the root unit when it is active or enabled. That
// needs the YNP rootsystemd sudoers rules.
func (h *InstallOtelCollector) tryStopRootOtelCollector(
	ctx context.Context,
) rootOtelCollectorStop {
	inUse, err := h.rootOtelCollectorNeedsStop(ctx)
	if err != nil {
		util.FileLogger().Warnf(ctx, "Failed to check the root-level otel-collector - %s", err)
		return rootOtelCollectorStateUnknown
	}
	if !inUse {
		return rootOtelCollectorNotRunning
	}
	util.FileLogger().Infof(ctx, "Stopping the root-level otel-collector")
	// One attempt each: a missing sudoers rule does not go away with retries. The disable runs
	// even if the stop fails, so the unit does not start at boot. Details go to the file log
	// only; stopRootOtelCollector writes the one task-output warning.
	var stopErr error
	for _, step := range rootOtelCollectorStopSteps() {
		steps := []struct {
			Desc string
			Cmd  string
		}{step}
		stopErr = errors.Join(stopErr, module.RunShellSteps(ctx, "", steps, nil))
	}
	if stopErr == nil {
		return rootOtelCollectorNotRunning
	}
	util.FileLogger().Warnf(
		ctx, "Failed to stop or disable the root-level otel-collector - %s", stopErr)
	// Either step can fail, so the state after it decides. A running unit needs no is-enabled.
	active, err := h.rootOtelCollectorState(ctx, "is-active")
	enabled := ""
	if err == nil && !rootOtelCollectorRunning(active) {
		enabled, err = h.rootOtelCollectorState(ctx, "is-enabled")
	}
	if err != nil {
		util.FileLogger().Warnf(ctx, "Failed to check the root-level otel-collector - %s", err)
	}
	return rootOtelCollectorStopOutcome(active, enabled, err)
}

// rootOtelCollectorStopSteps stop and disable the root unit. sudo -n fails at once without a
// sudoers rule, and the YNP rootsystemd rules name the unit without the .service suffix.
func rootOtelCollectorStopSteps() []struct {
	Desc string
	Cmd  string
} {
	return []struct {
		Desc string
		Cmd  string
	}{
		{"StopRootOtelCollector", "sudo -n systemctl stop " + otelCollectorRootUnitName},
		{"DisableRootOtelCollector", "sudo -n systemctl disable " + otelCollectorRootUnitName},
	}
}

// rootOtelCollectorStopOutcome sorts a failed root stop by the state after it. A running unit
// counts as failed even if the is-enabled read then failed.
func rootOtelCollectorStopOutcome(active, enabled string, checkErr error) rootOtelCollectorStop {
	if rootOtelCollectorRunning(active) {
		return rootOtelCollectorStopFailed
	}
	if checkErr != nil {
		return rootOtelCollectorStateUnknown
	}
	if rootOtelCollectorInUse(active, enabled) {
		return rootOtelCollectorStopFailed
	}
	return rootOtelCollectorNotRunning
}

// rootOtelCollectorNeedsStop reports whether the root unit is active or enabled. The check needs
// no sudo, so a root unit that is stopped and disabled needs no sudoers rules.
func (h *InstallOtelCollector) rootOtelCollectorNeedsStop(ctx context.Context) (bool, error) {
	hasRoot, err := h.hasRootOtelCollectorUnit()
	if err != nil || !hasRoot {
		return false, err
	}
	active, err := h.rootOtelCollectorState(ctx, "is-active")
	if err != nil {
		return false, err
	}
	// A running unit needs the stop even if is-enabled cannot be read.
	if rootOtelCollectorRunning(active) {
		return true, nil
	}
	enabled, err := h.rootOtelCollectorState(ctx, "is-enabled")
	if err != nil {
		return false, err
	}
	return rootOtelCollectorInUse(active, enabled), nil
}

// rootOtelCollectorState returns the output of systemctl is-active or is-enabled for the root
// unit. Both exit non-zero for a stopped or disabled unit, so only empty output is an error.
func (h *InstallOtelCollector) rootOtelCollectorState(
	ctx context.Context,
	check string,
) (string, error) {
	cmd := fmt.Sprintf("systemctl %s %s || true", check, otelCollectorRootUnitName)
	// No task output: stopRootOtelCollector writes the one warning.
	cmdInfo, err := module.RunShellCmd(ctx, h.username, "CheckRootOtelCollector", cmd, nil)
	if err != nil {
		return "", err
	}
	state := strings.TrimSpace(cmdInfo.StdOut.String())
	if state == "" {
		return "", fmt.Errorf(
			"systemctl %s %s returned no state: %s",
			check,
			otelCollectorRootUnitName,
			strings.TrimSpace(cmdInfo.StdErr.String()),
		)
	}
	return state, nil
}

// rootOtelCollectorRunning reports whether systemctl is-active shows a running unit.
func rootOtelCollectorRunning(active string) bool {
	switch active {
	case "active", "activating", "reloading", "deactivating", "refreshing":
		return true
	}
	return false
}

// rootOtelCollectorInUse reports whether the root unit runs now or starts at boot.
func rootOtelCollectorInUse(active, enabled string) bool {
	switch enabled {
	case "enabled", "enabled-runtime", "linked", "linked-runtime", "alias", "indirect":
		return true
	}
	switch active {
	case "inactive", "failed", "unknown":
		return false
	}
	return true
}

// ensureOtelCollectorDirStep creates <yb_home>/otel-collector. The package setup, the disable
// path and the log_cleanup_env write all use it, so they create the directory the same way.
func ensureOtelCollectorDirStep(ybHome string) struct {
	Desc string
	Cmd  string
} {
	dir := shellSingleQuote(filepath.Join(ybHome, "otel-collector"))
	return struct {
		Desc string
		Cmd  string
	}{
		"ensure-otel-collector-dir",
		fmt.Sprintf("mkdir -p %s && chmod 0755 %s", dir, dir),
	}
}

// disableOtelCollector stops the collector and removes its config without the package setup, so
// that a broken package cannot block turning export off.
func (h *InstallOtelCollector) disableOtelCollector(ctx context.Context, ybHome string) error {
	// The root stop goes first, so that a failed user stop cannot leave a root collector running.
	h.stopRootOtelCollector(ctx)
	stopUserUnit, err := h.userOtelCollectorNeedsStop(ctx)
	if err != nil {
		return err
	}
	if stopUserUnit {
		util.FileLogger().Infof(ctx, "Disable mode: stopping otel-collector")
		if err := module.DisableSystemdService(
			ctx,
			h.username,
			OtelCollectorService,
			"", // Keep the unit file so that a later enable can start the service again.
			h.logOut); err != nil {
			return err
		}
	}
	// configureOtelCollector writes into this directory, and this path skips the package setup.
	steps := []struct {
		Desc string
		Cmd  string
	}{
		ensureOtelCollectorDirStep(ybHome),
	}
	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	// No config file is sent in this mode, so this removes config.yml and the credentials.
	return h.configureOtelCollector(ctx, ybHome)
}

// refreshLogPurgeScript renders the bundled zip_purge_yb_logs.sh.j2 template
// and copies it into <yb_home>/bin/zip_purge_yb_logs.sh so that nodes always
// run the script version shipped with the current node-agent build. This is a
// no-op-equivalent on unchanged versions and lets script changes roll out via
// the existing ManageOtelCollector flow without requiring a separate
// ConfigureServer / software-upgrade trigger.
func (h *InstallOtelCollector) refreshLogPurgeScript(
	ctx context.Context,
	ybHome string,
) error {
	scriptContext := map[string]any{
		"yb_home_dir": ybHome,
		"user_name":   h.username,
	}
	_, err := module.CopyFile(
		ctx,
		scriptContext,
		filepath.Join(module.ServerTemplateSubpath, "zip_purge_yb_logs.sh.j2"),
		filepath.Join(ybHome, "bin", "zip_purge_yb_logs.sh"),
		fs.FileMode(0755),
		h.username,
	)
	return err
}

// logCleanupEnvSteps returns the shell steps that (re)generate the
// otel-collector/log_cleanup_env file consumed by zip_purge_yb_logs.sh. Kept
// separate so the refresh-only path can reuse it without pulling in the rest
// of the otel-collector configureOtelCollector cleanup steps.
func logCleanupEnvSteps(
	envPath, ycqlAuditLogLevel string,
	ysqlAuditLogRetentionDays, ycqlAuditLogRetentionDays uint32,
	ysqlAuditLineStartRegex string,
) []struct {
	Desc string
	Cmd  string
} {
	envPath = shellSingleQuote(envPath)
	return []struct {
		Desc string
		Cmd  string
	}{
		{
			"clean-up-otel-log-cleanup-env",
			fmt.Sprintf("rm -rf %s", envPath),
		},
		{
			"write-otel-log-cleanup-env",
			fmt.Sprintf(
				`echo "preserve_audit_logs=true" > %s `+
					`&& echo "ycql_audit_log_level=%s" >> %s `+
					`&& echo "ysql_audit_log_retention_days=%d" >> %s `+
					`&& echo "ycql_audit_log_retention_days=%d" >> %s`,
				envPath,
				ycqlAuditLogLevel,
				envPath,
				ysqlAuditLogRetentionDays,
				envPath,
				ycqlAuditLogRetentionDays,
				envPath,
			),
		},
		{
			"write-otel-log-cleanup-line-start-regex",
			// Single-quoted in the file so the sourcing shell treats the regex
			// literally; the whole line is shell-quoted again so the writer shell
			// emits it verbatim.
			fmt.Sprintf(
				"printf '%%s\\n' %s >> %s",
				shellSingleQuote("ysql_audit_line_start_regex="+
					shellSingleQuote(ysqlAuditLineStartRegex)),
				envPath,
			),
		},
		{
			"set-permission-otel-log-cleanup-env",
			fmt.Sprintf(`chmod 0440 %s`, envPath),
		},
	}
}

// shellSingleQuote wraps s in single quotes, escaping embedded single quotes,
// so it survives one layer of shell word-splitting/expansion verbatim.
func shellSingleQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
}

// writeLogCleanupEnv (re)generates <yb_home>/otel-collector/log_cleanup_env
// under a pre-existing otel-collector directory (created lazily here if
// missing). Used by the refresh-only path to propagate audit-log retention
// changes even when otel-collector isn't being (re)installed.
func (h *InstallOtelCollector) writeLogCleanupEnv(ctx context.Context, ybHome string) error {
	envPath := filepath.Join(ybHome, "otel-collector", "log_cleanup_env")
	steps := []struct {
		Desc string
		Cmd  string
	}{
		// Refresh-only mode can run on universes that never had otel-collector
		// installed, so ensure the directory exists before writing the env file.
		ensureOtelCollectorDirStep(ybHome),
	}
	steps = append(steps, logCleanupEnvSteps(envPath,
		h.param.GetYcqlAuditLogLevel(),
		h.param.GetYsqlAuditLogRetentionDays(),
		h.param.GetYcqlAuditLogRetentionDays(),
		h.param.GetYsqlAuditLineStartRegex())...)
	return module.RunShellSteps(ctx, h.username, steps, h.logOut)
}
