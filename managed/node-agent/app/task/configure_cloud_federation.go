// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
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
)

const (
	// YBC systemd unit whose environment we augment for cross-cloud storage access.
	YbControllerService = "yb-controller.service"

	// federationDirName is the dedicated directory under ~/.yugabyte that holds every federation
	// artifact (creds, env, stamp, scripts), so teardown and node cleanup can drop the whole folder
	// at once.
	federationDirName = "federation"

	// On-node artifact names (all live under ~/.yugabyte/federation, except the systemd drop-in and
	// the S3-on-GCP ~/.aws/config managed block).
	federationDropInFileName = "10-yb-federation.conf"
	federationEnvFileName    = "federation.env"
	gcpFedCredsFileName      = "gcp-fed-creds.json"
	// Stamp holding the desired-state hash last successfully applied AND restarted, used to
	// skip re-applying/restarting YBC when the node is already in the requested state.
	federationAppliedStampName = ".federation-applied"

	// Template subpaths (relative to resources/templates/, under ServerTemplateSubpath).
	federationEnvTemplate        = "yb-federation.env.j2"
	federationDropInTemplate     = "yb-controller-federation.conf.j2"
	gcpFedCredsTemplate          = "gcp-fed-creds.json.j2"
	awsCredentialProcessTemplate = "aws_credential_process.sh.j2"

	// S3-on-GCP artifacts: a credential_process script under the federation dir and a profile in
	// ~/.aws/config.
	awsCredentialProcessScriptName = "aws_credential_process.sh"
	// The block bracketing the managed credential_process profile in ~/.aws/config; teardown and
	// re-apply strip exactly this block, leaving any other profiles in the file untouched.
	awsConfigBlockBegin = "# BEGIN YB MANAGED BLOCK - CLOUD FEDERATION"
	awsConfigBlockEnd   = "# END YB MANAGED BLOCK - CLOUD FEDERATION"

	// Systemd drop-in directory name for the YBC unit (e.g. yb-controller.service.d).
	ybcServiceDropInDirName = YbControllerService + ".d"

	systemSystemdUnitDir = "/etc/systemd/system"
)

// federationDir is the directory under ~/.yugabyte that holds all federation artifacts.
func federationDir(ybHome string) string {
	return filepath.Join(ybHome, ".yugabyte", federationDirName)
}

// reconcileOutcome is the result of a reconcile, returned in
// ConfigureCloudFederationOutput.detail as a stable code (not a prose message), so YBA can match
// on it to decide what to do rather than treating it as free-form log text.
type reconcileOutcome string

const (
	outcomeAlreadyTornDown   reconcileOutcome = "ALREADY_TORN_DOWN"
	outcomeTornDown          reconcileOutcome = "TORN_DOWN"
	outcomeAlreadyConfigured reconcileOutcome = "ALREADY_CONFIGURED"
	outcomeReconciled        reconcileOutcome = "RECONCILED"
)

// ConfigureCloudFederation sets up (or tears down) cross-cloud federated IAM on
// a DB node. It deploys only static config; short-lived credentials are minted
// in-process by YBC's client libraries, so there is no refresher daemon.
type ConfigureCloudFederation struct {
	param    *pb.ConfigureCloudFederationInput
	username string
	logOut   util.Buffer
}

func NewConfigureCloudFederationHandler(
	param *pb.ConfigureCloudFederationInput,
	username string,
) *ConfigureCloudFederation {
	return &ConfigureCloudFederation{
		param:    param,
		username: username,
		logOut:   util.NewBuffer(module.MaxBufferCapacity),
	}
}

// CurrentTaskStatus implements the AsyncTask method.
func (h *ConfigureCloudFederation) CurrentTaskStatus() *TaskStatus {
	return &TaskStatus{
		Info:       h.logOut,
		ExitStatus: &ExitStatus{},
	}
}

func (h *ConfigureCloudFederation) String() string {
	return "Configure Cloud Federation Task"
}

func (h *ConfigureCloudFederation) Handle(ctx context.Context) (*pb.DescribeTaskResponse, error) {
	ybHome := h.param.GetYbHomeDir()
	if ybHome == "" {
		err := errors.New("ybHomeDir is required")
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	util.FileLogger().Infof(
		ctx,
		"Reconciling cloud federation (direction=%s, enabled=%t)",
		h.param.GetFlowDirection(),
		h.param.GetEnabled(),
	)

	changed, detail, err := h.reconcile(ctx, ybHome)
	if err != nil {
		util.FileLogger().Error(ctx, err.Error())
		return nil, err
	}
	util.FileLogger().Infof(ctx, "Cloud federation reconcile: changed=%t (%s)", changed, detail)
	return &pb.DescribeTaskResponse{
		Data: &pb.DescribeTaskResponse_ConfigureCloudFederationOutput{
			ConfigureCloudFederationOutput: &pb.ConfigureCloudFederationOutput{
				Changed: changed,
				Detail:  string(detail),
			},
		},
	}, nil
}

// reconcile brings the node to the desired federation state and restarts YBC only when something
// changed. YBC loads the federation env (GOOGLE_APPLICATION_CREDENTIALS / AWS_PROFILE) only at
// (re)start, so a restart is mandatory on drift and can be skipped when the state already matches.
func (h *ConfigureCloudFederation) reconcile(
	ctx context.Context,
	ybHome string,
) (bool, reconcileOutcome, error) {
	if !h.param.GetEnabled() {
		if !h.federationArtifactsPresent(ybHome) {
			return false, outcomeAlreadyTornDown, nil
		}
		if err := h.teardown(ctx, ybHome); err != nil {
			return false, "", err
		}
		if err := h.restartYbc(ctx); err != nil {
			return false, "", err
		}
		return true, outcomeTornDown, nil
	}

	desired, err := h.desiredStateHash(ybHome)
	if err != nil {
		return false, "", err
	}
	if h.inSync(ybHome, desired) {
		return false, outcomeAlreadyConfigured, nil
	}

	if err := h.setup(ctx, ybHome); err != nil {
		return false, "", err
	}
	if err := h.restartYbc(ctx); err != nil {
		return false, "", err
	}
	// Write the stamp only AFTER a successful restart, so a failure between the writes and the
	// restart is re-detected as drift on the next reconcile (never "disk matches but YBC stale").
	if err := h.writeAppliedStamp(ctx, ybHome, desired); err != nil {
		util.FileLogger().Warnf(ctx, "Failed to write federation stamp: %s", err.Error())
	}
	return true, outcomeReconciled, nil
}

func (h *ConfigureCloudFederation) restartYbc(ctx context.Context) error {
	return module.ControlSystemdService(ctx, h.username, YbControllerService, "restart", h.logOut)
}

// desiredStateHash is a stable digest of the inputs that fully determine every on-node artifact.
// Templates are deterministic, so identical inputs render identical files; comparing the hash to
// the applied stamp is equivalent to comparing rendered content, without re-rendering here.
func (h *ConfigureCloudFederation) desiredStateHash(ybHome string) (string, error) {
	var canonical string
	switch h.param.GetFlowDirection() {
	case pb.ConfigureCloudFederationInput_GCS_ON_AWS:
		cfg := h.param.GetGcsOnAws()
		if err := validateGcsOnAwsInputs(cfg); err != nil {
			return "", err
		}
		canonical = fmt.Sprintf("v1|gcs|%s|%s", ybHome, cfg.GetAudience())
	case pb.ConfigureCloudFederationInput_S3_ON_GCP:
		cfg := h.param.GetS3OnGcp()
		if err := validateS3OnGcpInputs(cfg); err != nil {
			return "", err
		}
		canonical = fmt.Sprintf(
			"v1|s3|%s|%s|%s|%s",
			ybHome,
			cfg.GetRoleArn(),
			cfg.GetAudience(),
			cfg.GetProfileName(),
		)
	default:
		return "", fmt.Errorf("unsupported flow direction: %s", h.param.GetFlowDirection())
	}
	sum := sha256.Sum256([]byte(canonical))
	return hex.EncodeToString(sum[:]), nil
}

// inSync reports whether the node is already configured for desiredHash: the applied stamp matches
// AND the artifacts a running YBC depends on are present.
func (h *ConfigureCloudFederation) inSync(ybHome, desiredHash string) bool {
	fedDir := federationDir(ybHome)
	stampFile := filepath.Join(fedDir, federationAppliedStampName)
	data, err := os.ReadFile(stampFile)
	if err != nil || strings.TrimSpace(string(data)) != desiredHash {
		return false
	}
	if !federationPathExists(filepath.Join(fedDir, federationEnvFileName)) {
		return false
	}
	dropInDir, _, err := h.dropInDir()
	if err != nil || !federationPathExists(filepath.Join(dropInDir, federationDropInFileName)) {
		return false
	}
	switch h.param.GetFlowDirection() {
	case pb.ConfigureCloudFederationInput_GCS_ON_AWS:
		return federationPathExists(filepath.Join(fedDir, gcpFedCredsFileName))
	case pb.ConfigureCloudFederationInput_S3_ON_GCP:
		// The credential_process script and the managed ~/.aws/config block are the artifacts YBC
		// depends on; a hand-removed block must count as drift even when the stamp matches.
		return federationPathExists(filepath.Join(fedDir, awsCredentialProcessScriptName)) &&
			awsManagedBlockPresent(filepath.Join(ybHome, ".aws", "config"))
	}
	return false
}

// federationArtifactsPresent reports whether any federation artifact still exists on the node
// (used to make teardown a no-op when there is nothing to remove).
func (h *ConfigureCloudFederation) federationArtifactsPresent(ybHome string) bool {
	// The whole federation directory is dropped on teardown, so a non-empty directory means
	// artifacts remain. An empty directory is treated as nothing to tear down.
	if dirHasEntries(federationDir(ybHome)) {
		return true
	}
	// The S3-on-GCP managed block lives in ~/.aws/config, outside the federation dir, so a leftover
	// block still counts as an artifact that needs tearing down.
	if awsManagedBlockPresent(filepath.Join(ybHome, ".aws", "config")) {
		return true
	}
	if dropInDir, _, err := h.dropInDir(); err == nil {
		return federationPathExists(filepath.Join(dropInDir, federationDropInFileName))
	}
	return false
}

func (h *ConfigureCloudFederation) writeAppliedStamp(
	ctx context.Context,
	ybHome, hash string,
) error {
	stampFile := filepath.Join(federationDir(ybHome), federationAppliedStampName)
	return module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{
			"write-federation-stamp",
			fmt.Sprintf("printf '%%s' '%s' > %s && chmod 0600 %s", hash, stampFile, stampFile),
		},
	}, h.logOut)
}

func federationPathExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// dirHasEntries reports whether path is a directory that contains at least one entry. An empty
// (or missing) directory counts as "no artifacts", so a stray empty folder never triggers a
// teardown + YBC restart.
func dirHasEntries(path string) bool {
	entries, err := os.ReadDir(path)
	return err == nil && len(entries) > 0
}

// setup writes the credential artifact(s) for the active flow, the env file consumed by YBC, and
// the systemd drop-in that points YBC at that env file.
func (h *ConfigureCloudFederation) setup(ctx context.Context, ybHome string) error {
	fedDir := federationDir(ybHome)
	envFile := filepath.Join(fedDir, federationEnvFileName)

	if err := module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		// Dedicated 0700 directory: it holds only federation artifacts, so it can be locked down
		// and dropped wholesale on teardown / node cleanup.
		{"create-federation-dir", fmt.Sprintf("mkdir -p %s && chmod 0700 %s", fedDir, fedDir)},
	}, h.logOut); err != nil {
		return err
	}

	var envCtx map[string]any
	switch h.param.GetFlowDirection() {
	case pb.ConfigureCloudFederationInput_GCS_ON_AWS:
		cfg := h.param.GetGcsOnAws()
		if err := validateGcsOnAwsInputs(cfg); err != nil {
			return err
		}
		credsFile := filepath.Join(fedDir, gcpFedCredsFileName)
		if _, err := module.CopyFile(
			ctx,
			map[string]any{"audience": cfg.GetAudience()},
			filepath.Join(module.ServerTemplateSubpath, gcpFedCredsTemplate),
			credsFile,
			fs.FileMode(0600),
			h.username,
		); err != nil {
			return err
		}
		envCtx = map[string]any{"flow": "gcs", "gcp_creds_path": credsFile}

	case pb.ConfigureCloudFederationInput_S3_ON_GCP:
		cfg := h.param.GetS3OnGcp()
		if err := validateS3OnGcpInputs(cfg); err != nil {
			return err
		}
		if err := h.writeS3Artifacts(ctx, ybHome, cfg); err != nil {
			return err
		}
		envCtx = map[string]any{
			"flow":            "s3",
			"aws_profile":     cfg.GetProfileName(),
			"aws_config_path": filepath.Join(ybHome, ".aws", "config"),
		}

	default:
		return fmt.Errorf("unsupported flow direction: %s", h.param.GetFlowDirection())
	}

	if _, err := module.CopyFile(
		ctx,
		envCtx,
		filepath.Join(module.ServerTemplateSubpath, federationEnvTemplate),
		envFile,
		fs.FileMode(0600),
		h.username,
	); err != nil {
		return err
	}

	return h.writeDropIn(ctx, envFile)
}

// writeDropIn installs the systemd drop-in that adds EnvironmentFile to the YBC
// unit. It handles both user-level (~/.config/systemd/user) and system-level
// (/etc/systemd/system, via sudo) systemd.
func (h *ConfigureCloudFederation) writeDropIn(ctx context.Context, envFile string) error {
	dropInCtx := map[string]any{"env_file": envFile}
	dropInDir, isUser, err := h.dropInDir()
	if err != nil {
		return err
	}
	dropInDest := filepath.Join(dropInDir, federationDropInFileName)

	if isUser {
		if err := module.RunShellSteps(ctx, h.username, []struct {
			Desc string
			Cmd  string
		}{
			{"create-federation-dropin-dir", fmt.Sprintf("mkdir -p %s", dropInDir)},
		}, h.logOut); err != nil {
			return err
		}
		_, err := module.CopyFile(
			ctx,
			dropInCtx,
			filepath.Join(module.ServerTemplateSubpath, federationDropInTemplate),
			dropInDest,
			fs.FileMode(0644),
			h.username,
		)
		return err
	}

	// System-level systemd: render to a YBA-provided writable temp dir (some nodes have a
	// non-writable /tmp), then sudo-install it under /etc/systemd/system where the user cannot write
	// directly. The temp copy is removed at the end of this step.
	remoteTmp := h.param.GetRemoteTmp()
	if remoteTmp == "" {
		remoteTmp = "/tmp"
	}
	tmpDropIn := filepath.Join(remoteTmp, federationDropInFileName)
	if _, err := module.CopyFile(
		ctx,
		dropInCtx,
		filepath.Join(module.ServerTemplateSubpath, federationDropInTemplate),
		tmpDropIn,
		fs.FileMode(0644),
		h.username,
	); err != nil {
		return err
	}
	return module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"create-federation-dropin-dir", fmt.Sprintf("sudo mkdir -p %s", dropInDir)},
		{
			"install-federation-dropin",
			fmt.Sprintf(
				"sudo cp %s %s && sudo chmod 0644 %s",
				tmpDropIn,
				dropInDest,
				dropInDest,
			),
		},
		{"cleanup-tmp-federation-dropin", fmt.Sprintf("rm -f %s", tmpDropIn)},
	}, h.logOut)
}

// teardown removes the federation directory (all artifacts) and the systemd drop-in. The S3-on-GCP
// managed block lives in ~/.aws/config, outside that directory, so it is stripped separately.
func (h *ConfigureCloudFederation) teardown(ctx context.Context, ybHome string) error {
	if err := h.removeDropIn(ctx); err != nil {
		return err
	}
	// The systemd drop-in must live under yb-controller.service.d and is removed above; every other
	// federation artifact is contained in the federation directory, so a single recursive delete
	// clears it. The one exception is the S3-on-GCP managed block in the shared ~/.aws/config, so
	// strip only that block (safe no-op when the file/block is absent), leaving other profiles.
	return module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"remove-federation-dir", fmt.Sprintf("rm -rf %s", federationDir(ybHome))},
		{"remove-federation-aws-block", removeAwsBlockCmd(filepath.Join(ybHome, ".aws", "config"))},
	}, h.logOut)
}

func (h *ConfigureCloudFederation) removeDropIn(ctx context.Context) error {
	dropInDir, isUser, err := h.dropInDir()
	if err != nil {
		return err
	}
	dropInDest := filepath.Join(dropInDir, federationDropInFileName)
	rm := fmt.Sprintf("rm -f %s", dropInDest)
	if !isUser {
		rm = fmt.Sprintf("sudo rm -f %s", dropInDest)
	}
	return module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"remove-federation-dropin", rm},
	}, h.logOut)
}

// dropInDir returns the yb-controller.service.d directory for the active systemd
// mode and whether it is user-level.
func (h *ConfigureCloudFederation) dropInDir() (string, bool, error) {
	isUser, _, err := module.IsUserSystemd(h.username, YbControllerService)
	if err != nil {
		return "", false, err
	}
	if isUser {
		info, err := util.UserInfo(h.username)
		if err != nil {
			return "", false, err
		}
		return filepath.Join(
			info.User.HomeDir,
			module.UserSystemdUnitPath,
			ybcServiceDropInDirName,
		), true, nil
	}
	return filepath.Join(systemSystemdUnitDir, ybcServiceDropInDirName), false, nil
}

// Audience allowlist for GCS-on-AWS: interpolated into the external_account JSON
// template, so the charset excludes quotes/backslashes/whitespace (JSON-safe).
var gcpAudienceRegex = regexp.MustCompile(`^[A-Za-z0-9._:/-]{1,512}$`)

// validateGcsOnAwsInputs rejects an empty or malformed audience before it is
// rendered into the external_account JSON template.
func validateGcsOnAwsInputs(cfg *pb.GcsOnAwsConfig) error {
	if cfg == nil {
		return errors.New("gcsOnAws config is required for GCS_ON_AWS")
	}
	if !gcpAudienceRegex.MatchString(cfg.GetAudience()) {
		return errors.New("invalid or empty audience for GCS-on-AWS federation")
	}
	return nil
}

// S3-on-GCP allowlists: roleArn/profileName are interpolated into aws_credential_process.sh and the
// echo'd ~/.aws/config block, so their charsets exclude quotes/backslashes/whitespace to keep the
// rendered shell safe; audience reuses the JSON-safe GCS charset (gcpAudienceRegex).
var (
	// Restricted to the standard 'aws' partition on purpose: the credential_process script and
	// YBA both use the global STS endpoint, which aws-cn and aws-us-gov do not serve. Accepting
	// those ARNs here would defer the failure to an opaque STS error at backup time.
	awsRoleArnRegex = regexp.MustCompile(
		`^arn:aws:iam::[0-9]{12}:role/[A-Za-z0-9._/+=,@-]{1,256}$`,
	)
	awsProfileRegex = regexp.MustCompile(`^[A-Za-z0-9._-]{1,128}$`)
)

// validateS3OnGcpInputs rejects empty or malformed inputs before they are rendered into the
// credential_process script and the ~/.aws/config profile.
func validateS3OnGcpInputs(cfg *pb.S3OnGcpConfig) error {
	if cfg == nil {
		return errors.New("s3OnGcp config is required for S3_ON_GCP")
	}
	if !awsRoleArnRegex.MatchString(cfg.GetRoleArn()) {
		return errors.New("invalid or empty roleArn for S3-on-GCP federation")
	}
	if !gcpAudienceRegex.MatchString(cfg.GetAudience()) {
		return errors.New("invalid or empty audience for S3-on-GCP federation")
	}
	if !awsProfileRegex.MatchString(cfg.GetProfileName()) {
		return errors.New("invalid or empty profileName for S3-on-GCP federation")
	}
	return nil
}

// awsManagedBlockPresent reports whether the managed credential_process block is present in the
// given ~/.aws/config. Pure read, so it is safe to call from inSync / federationArtifactsPresent.
func awsManagedBlockPresent(awsConfig string) bool {
	data, err := os.ReadFile(awsConfig)
	if err != nil {
		return false
	}
	return strings.Contains(string(data), awsConfigBlockBegin)
}

// writeS3Artifacts renders the credential_process script under the federation dir and installs the
// managed credential_process profile in ~/.aws/config. Idempotent: the existing managed block is
// stripped before the new one is appended, so re-running on drift never duplicates it.
func (h *ConfigureCloudFederation) writeS3Artifacts(
	ctx context.Context,
	ybHome string,
	cfg *pb.S3OnGcpConfig,
) error {
	fedDir := federationDir(ybHome)
	scriptPath := filepath.Join(fedDir, awsCredentialProcessScriptName)
	awsDir := filepath.Join(ybHome, ".aws")
	awsConfig := filepath.Join(awsDir, "config")

	// The federation dir is already created (0700) by setup; only ~/.aws needs to be ensured here.
	if err := module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"create-aws-dir", fmt.Sprintf("mkdir -p %s && chmod 0700 %s", awsDir, awsDir)},
	}, h.logOut); err != nil {
		return err
	}

	if _, err := module.CopyFile(
		ctx,
		map[string]any{
			"audience": cfg.GetAudience(),
			"role_arn": cfg.GetRoleArn(),
		},
		filepath.Join(module.ServerTemplateSubpath, awsCredentialProcessTemplate),
		scriptPath,
		fs.FileMode(0750),
		h.username,
	); err != nil {
		return err
	}

	return module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"remove-existing-federation-aws-block", removeAwsBlockCmd(awsConfig)},
		{
			"append-federation-aws-block",
			fmt.Sprintf(
				`echo '%s
[profile %s]
credential_process = %s
%s' >> %s && chmod 0600 %s`,
				awsConfigBlockBegin,
				cfg.GetProfileName(),
				scriptPath,
				awsConfigBlockEnd,
				awsConfig,
				awsConfig,
			),
		},
	}, h.logOut)
}

// removeAwsBlockCmd returns a shell command that strips the managed block from ~/.aws/config,
// leaving any other profiles intact. Safe when the file or the block is absent.
func removeAwsBlockCmd(awsConfig string) string {
	return fmt.Sprintf(
		`if [ -f %s ]; then `+
			`awk '/%s/ {inblock=1} /%s/ {inblock=0; next} !inblock' %s > %s.tmp && `+
			`mv %s.tmp %s; fi`,
		awsConfig,
		awsConfigBlockBegin,
		awsConfigBlockEnd,
		awsConfig,
		awsConfig,
		awsConfig,
		awsConfig,
	)
}
