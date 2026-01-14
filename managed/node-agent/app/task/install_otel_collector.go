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
	"path/filepath"
)

const OtelCollectorService = "otel-collector.service"

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

	// 2) Put & setup the otel collector.
	err := h.execOtelCollectorSetupSteps(ctx, h.param.GetYbHomeDir())
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
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
	}

	// Copy otel-collector.service
	_, err = module.CopyFile(
		ctx,
		otelCollectorServiceContext,
		filepath.Join(module.ServerTemplateSubpath, OtelCollectorService),
		filepath.Join(h.param.GetYbHomeDir(), module.UserSystemdUnitPath, OtelCollectorService),
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

	// 6) Start and enable the service only if config file exists
	if h.param.GetOtelColConfigFile() != "" {
		if err = module.StartSystemdService(ctx, h.username, OtelCollectorService, h.logOut); err != nil {
			return nil, err
		}
		if err = module.EnableSystemdService(ctx, h.username, OtelCollectorService, h.logOut); err != nil {
			return nil, err
		}
	}
	return nil, nil
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
			"ensure 755 permission for otelcol-contrib",
			fmt.Sprintf(
				"chmod -R 755 %s",
				filepath.Join(otelCollectorDirectory, "otelcol-contrib"),
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
			"clean-up-otel-log-cleanup-env",
			fmt.Sprintf("rm -rf %s", otelColLogCleanupEnv),
		},
		{
			"write-otel-log-cleanup-env",
			fmt.Sprintf(
				`echo "preserve_audit_logs=true" > %s && echo "ycql_audit_log_level=%s" >> %s`,
				otelColLogCleanupEnv,
				h.param.GetYcqlAuditLogLevel(),
				otelColLogCleanupEnv,
			),
		},
		{
			"set-permission-otel-log-cleanup-env",
			fmt.Sprintf(`chmod 0440 %s`, otelColLogCleanupEnv),
		},
	}

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
	return nil
}
