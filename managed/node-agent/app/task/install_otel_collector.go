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
	"sort"
	"strings"
	"syscall"
)

const OtelCollectorService = "otel-collector.service"

const (
	// Name of the executable inside the published archive. The YugabyteDB unified collector is a
	// custom ocb distribution rather than the upstream contrib one, so it does not ship under the
	// contrib name.
	otelCollectorPackagedBinary = "otelcol-unified"
	// Name the executable is installed under on the node. This becomes the running process name,
	// which the systemd unit, the health check's `ps -C` lookup and the `process` label on the
	// per-process metrics all key off, so it must stay stable across collector version changes.
	otelCollectorInstalledBinary = "otelcol-contrib"
	// Holds the exporter credentials that config.yml references as ${env:NAME}.
	otelCollectorSecretEnvFile = "secrets.env"
)

type InstallOtelCollector struct {
	shellTask *ShellTask
	param     *pb.InstallOtelCollectorInput
	username  string
	logOut    util.Buffer
}

func NewInstallOtelCollectorHandler(
	param *pb.InstallOtelCollectorInput,
	username string,
) *InstallOtelCollector {
	return &InstallOtelCollector{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
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
		if err := h.refreshLogPurgeScript(ctx, h.param.GetYbHomeDir()); err != nil {
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
		"otel_col_gcp_creds_present":  h.param.GetOtelColGcpCredsFile() != "",
		"otel_col_secret_env_present": len(h.param.GetOtelColSecretEnv()) > 0,
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

	// 4) Stop and disable the systemd service
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
	err = h.refreshLogPurgeScript(ctx, h.param.GetYbHomeDir())
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
	// The new config may reference credentials that are not in the env file yet.
	if _, err := module.RunShellCmdWithEnv(
		ctx, h.username, "ValidateOtelCollectorConfig", cmd, h.secretEnvList(), h.logOut,
	); err != nil {
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
		{
			"make-yb-otel-collector-dir",
			fmt.Sprintf(
				"mkdir -p %s && chmod 0755 %s",
				otelCollectorDirectory,
				otelCollectorDirectory,
			),
		},
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
	otelCollectorConfigFile := filepath.Join(ybHome, "otel-collector", "config.yml")
	otelColLogCleanupEnv := filepath.Join(ybHome, "otel-collector", "log_cleanup_env")
	awsCredsFile := filepath.Join(ybHome, ".aws", "credentials")
	gcpCredsFile := filepath.Join(ybHome, "otel-collector", "gcp_creds")
	secretEnvFile := filepath.Join(ybHome, "otel-collector", otelCollectorSecretEnvFile)

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
			fmt.Sprintf("mkdir -p %s/.aws", ybHome),
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
		{
			"remove-otel-collector-secret-env",
			fmt.Sprintf("rm -f %s", secretEnvFile),
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

	if h.param.GetOtelColConfigFile() != "" {
		steps = append(steps, struct {
			Desc string
			Cmd  string
		}{
			"place-new-otel-collector-config-file",
			fmt.Sprintf(
				"mv %s %s",
				h.param.GetOtelColConfigFile(),
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
			fmt.Sprintf("mv %s %s", h.param.GetOtelColGcpCredsFile(), gcpCredsFile),
		})
	}

	if err := module.RunShellSteps(ctx, h.username, steps, h.logOut); err != nil {
		return err
	}
	// Credentials are written in Go so the values never appear on a command line.
	if h.param.GetOtelColAwsAccessKey() != "" && h.param.GetOtelColAwsSecretKey() != "" {
		if err := h.appendAwsCredsBlock(ctx, awsCredsFile); err != nil {
			return err
		}
	}
	if len(h.param.GetOtelColSecretEnv()) > 0 {
		return h.writeSecretEnvFile(ctx, secretEnvFile)
	}
	return nil
}

// sortedSecretEnvKeys returns the names of the exporter credential env vars, sorted.
func (h *InstallOtelCollector) sortedSecretEnvKeys() []string {
	keys := make([]string, 0, len(h.param.GetOtelColSecretEnv()))
	for key := range h.param.GetOtelColSecretEnv() {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

// secretEnvList returns the exporter credentials as KEY=VALUE pairs, sorted by key.
func (h *InstallOtelCollector) secretEnvList() []string {
	env := []string{}
	for _, key := range h.sortedSecretEnvKeys() {
		env = append(env, key+"="+h.param.GetOtelColSecretEnv()[key])
	}
	return env
}

// writeSecretEnvFile writes the credentials in systemd EnvironmentFile format, readable only by
// the collector user. An earlier step removes the file, so it must not exist here.
func (h *InstallOtelCollector) writeSecretEnvFile(ctx context.Context, path string) error {
	var content strings.Builder
	for _, key := range h.sortedSecretEnvKeys() {
		value := h.param.GetOtelColSecretEnv()[key]
		content.WriteString(key + "=" + systemdEnvQuote(value) + "\n")
	}
	return h.writeUserFile(ctx, path, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600, content.String())
}

// appendAwsCredsBlock appends the collector profile to the AWS credentials file. An earlier
// step removes the old block.
func (h *InstallOtelCollector) appendAwsCredsBlock(ctx context.Context, path string) error {
	block := "# BEGIN YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n" +
		"[otel-collector]\n" +
		"aws_access_key_id = " + h.param.GetOtelColAwsAccessKey() + "\n" +
		"aws_secret_access_key = " + h.param.GetOtelColAwsSecretKey() + "\n" +
		"# END YB MANAGED BLOCK - OTEL COLLECTOR CREDENTIALS\n"
	return h.writeUserFile(ctx, path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0440, block)
}

// writeUserFile writes content to path and gives the file to the task user with the mode.
// O_NOFOLLOW stops a root node-agent from writing through a symlink the user planted.
func (h *InstallOtelCollector) writeUserFile(
	ctx context.Context,
	path string,
	flag int,
	mode os.FileMode,
	content string,
) error {
	userDetail, err := util.UserInfo(h.username)
	if err != nil {
		return err
	}
	file, err := os.OpenFile(path, flag|syscall.O_NOFOLLOW, mode)
	if err != nil {
		util.FileLogger().Errorf(ctx, "Error in opening file %s - %s", path, err.Error())
		return err
	}
	defer file.Close()
	if _, err = file.WriteString(content); err != nil {
		return err
	}
	if err = file.Chmod(mode); err != nil {
		return err
	}
	if !userDetail.IsCurrent {
		if err = file.Chown(int(userDetail.UserID), int(userDetail.GroupID)); err != nil {
			return err
		}
	}
	return nil
}

// systemdEnvQuote double-quotes a value for a systemd EnvironmentFile. Inside double quotes,
// systemd keeps the character after a backslash only for these four, so only they are escaped.
func systemdEnvQuote(value string) string {
	var quoted strings.Builder
	quoted.WriteByte('"')
	for _, r := range value {
		if strings.ContainsRune("\\\"`$", r) {
			quoted.WriteByte('\\')
		}
		quoted.WriteRune(r)
	}
	quoted.WriteByte('"')
	return quoted.String()
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
	otelDir := filepath.Join(ybHome, "otel-collector")
	envPath := filepath.Join(otelDir, "log_cleanup_env")
	steps := []struct {
		Desc string
		Cmd  string
	}{
		// Refresh-only mode can run on universes that never had otel-collector
		// installed, so ensure the directory exists before writing the env file.
		{
			"ensure-otel-collector-dir",
			fmt.Sprintf("mkdir -p %s && chmod 0755 %s", otelDir, otelDir),
		},
	}
	steps = append(steps, logCleanupEnvSteps(envPath,
		h.param.GetYcqlAuditLogLevel(),
		h.param.GetYsqlAuditLogRetentionDays(),
		h.param.GetYcqlAuditLogRetentionDays(),
		h.param.GetYsqlAuditLineStartRegex())...)
	return module.RunShellSteps(ctx, h.username, steps, h.logOut)
}
