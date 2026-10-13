// Copyright (c) YugabyteDB, Inc.

package task

import (
	"context"
	"errors"
	"fmt"
	"io/fs"
	"node-agent/app/federation/ocitoken"
	"node-agent/app/task/module"
	pb "node-agent/generated/service"
	"node-agent/util"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

// A node running on OCI reaches GCS by presenting an identity-domain JWT through a GCP
// external_account credential. Unlike the AWS source, whose credential_source GCP's libraries
// resolve on demand from the node's own metadata service, OCI has no such endpoint: the JWT must
// be minted by exchanging the node's instance principal. That token is short-lived, so a systemd
// timer keeps a file fresh and the credential reads it.
const (
	ociTokenFileName        = "oci-identity-token.jwt"
	ociTokenServiceName     = "yb-federation-oci-token.service"
	ociTokenTimerName       = "yb-federation-oci-token.timer"
	gcpFedCredsOciTemplate  = "gcp-fed-creds-oci.json.j2"
	ociTokenServiceTemplate = ociTokenServiceName + ".j2"
	ociTokenTimerTemplate   = ociTokenTimerName + ".j2"
)

const (
	// The timer refreshes every 20 minutes (see yb-federation-oci-token.timer.j2). A token must outlive
	// that gap with room to spare, so setup refuses one valid for less than this.
	ociTokenMinLifetime = 30 * time.Minute
	// A token with less than this left means the refresher is not keeping up. A healthy one, right
	// before its next run, still has the minimum lifetime less one refresh interval (10 minutes), so
	// this sits below that.
	ociTokenMinRemaining = 5 * time.Minute
)

// ociTokenFresh reports whether the token file holds a token with time left. It reads the token's
// own expiry rather than the file's age, which says nothing about how long the token lives. Without
// it a stopped timer, or an outage that outlasted the token, would look like a configured node and
// never be repaired. A pure read, safe from inSync.
func ociTokenFresh(tokenFile string) bool {
	token, err := ocitoken.ReadTokenFile(tokenFile)
	return err == nil && ocitoken.RequireLifetime(token, ociTokenMinRemaining, time.Now()) == nil
}

// validateFederationRoute rejects a (source, target) pair no flow serves. A node pointed at an
// unsupported bucket would be configured with credentials that cannot work. An unset source is
// accepted: it is what a YBA that predates the field sends, and the target alone then decides.
func validateFederationRoute(source, target pb.ConfigureCloudFederationInput_CloudProvider) error {
	if source == pb.ConfigureCloudFederationInput_CLOUD_PROVIDER_UNSPECIFIED {
		return nil
	}
	switch {
	case source == pb.ConfigureCloudFederationInput_AWS &&
		target == pb.ConfigureCloudFederationInput_GCP,
		source == pb.ConfigureCloudFederationInput_GCP &&
			target == pb.ConfigureCloudFederationInput_AWS,
		source == pb.ConfigureCloudFederationInput_OCI &&
			target == pb.ConfigureCloudFederationInput_GCP:
		return nil
	}
	return fmt.Errorf("no federation route from %s to %s", source, target)
}

// validateOciSourceInputs rejects a missing or malformed identity domain before it is written
// into a unit file or sent a request signed with the node's credentials.
func validateOciSourceInputs(cfg *pb.OciSourceConfig) error {
	if cfg == nil {
		return errors.New("ociSource config is required when the source cloud is OCI")
	}
	if err := ocitoken.ValidateIdentityDomainURL(cfg.GetIdentityDomainUrl()); err != nil {
		return fmt.Errorf("invalid identity domain for OCI federation: %w", err)
	}
	return nil
}

// unitPathRegex is what a path interpolated into a systemd unit may contain. systemd parses the
// unit, and treats a quote, a newline, a % specifier or a $ expansion as syntax, so any of them in
// a path could end the value early or inject a directive. The paths come from the provider's
// ybHome, which an admin sets, so they are checked rather than trusted.
var unitPathRegex = regexp.MustCompile(`^/[A-Za-z0-9._/@+-]+$`)

func validateUnitPath(what, path string) error {
	if !unitPathRegex.MatchString(path) {
		return fmt.Errorf(
			"%s %q cannot be used in a systemd unit: it must be an absolute path made of letters, "+
				"digits and . _ / @ + - only", what, path)
	}
	return nil
}

func (h *ConfigureCloudFederation) isOciSource() bool {
	return h.param.GetSourceCloud() == pb.ConfigureCloudFederationInput_OCI
}

// ociRefresherPresent reports whether any of the refresher's units is installed, which is what
// decides whether teardown has something to remove. A pure read, safe from inSync and
// federationArtifactsPresent.
func (h *ConfigureCloudFederation) ociRefresherPresent() bool {
	unitDir, _, err := h.systemdUnitDir()
	if err != nil {
		return false
	}
	return federationPathExists(filepath.Join(unitDir, ociTokenTimerName)) ||
		federationPathExists(filepath.Join(unitDir, ociTokenServiceName))
}

// ociRefresherComplete reports whether both units are installed. A node holding only one has a
// refresher that cannot run, so that is drift, not a configured node.
func (h *ConfigureCloudFederation) ociRefresherComplete() bool {
	unitDir, _, err := h.systemdUnitDir()
	if err != nil {
		return false
	}
	return federationPathExists(filepath.Join(unitDir, ociTokenTimerName)) &&
		federationPathExists(filepath.Join(unitDir, ociTokenServiceName))
}

// setupOciSource brings up everything an OCI node needs to present an identity to GCP, and returns
// the credential file path. The first token is fetched synchronously and as the node's user before
// anything else is installed: a wrong identity domain, or a node with no instance principal, then
// fails the federation task with the reason, instead of surfacing an hour later as a failed backup.
func (h *ConfigureCloudFederation) setupOciSource(
	ctx context.Context,
	fedDir string,
	audience string,
) (string, error) {
	cfg := h.param.GetOciSource()
	if err := validateOciSourceInputs(cfg); err != nil {
		return "", err
	}
	tokenFile := filepath.Join(fedDir, ociTokenFileName)
	identityDomainURL := cfg.GetIdentityDomainUrl()
	// Checked before the first fetch: both paths are written into the refresher's unit.
	if err := validateUnitPath("node-agent binary path", util.NodeAgentBinaryPath()); err != nil {
		return "", err
	}
	if err := validateUnitPath("token file path", tokenFile); err != nil {
		return "", err
	}

	if err := module.RunShellSteps(ctx, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{
			"fetch-initial-oci-identity-token",
			fmt.Sprintf(
				"%s federation oci-token --identity-domain-url %s --output %s --min-lifetime %s",
				shellSingleQuote(util.NodeAgentBinaryPath()),
				shellSingleQuote(identityDomainURL),
				shellSingleQuote(tokenFile),
				ociTokenMinLifetime,
			),
		},
	}, h.logOut); err != nil {
		return "", fmt.Errorf("could not obtain an OCI identity token for federation: %w", err)
	}

	credsFile := filepath.Join(fedDir, gcpFedCredsFileName)
	if _, err := module.CopyFile(
		ctx,
		map[string]any{"audience": audience, "token_file": tokenFile},
		filepath.Join(module.ServerTemplateSubpath, gcpFedCredsOciTemplate),
		credsFile,
		fs.FileMode(0600),
		h.username,
	); err != nil {
		return "", err
	}
	if err := h.installOciRefresher(ctx, identityDomainURL, tokenFile); err != nil {
		return "", err
	}
	return credsFile, nil
}

// installOciRefresher installs the oneshot service and the timer that re-runs it, in whichever
// systemd mode this node uses for YBC, and (re)starts the timer. Idempotent.
func (h *ConfigureCloudFederation) installOciRefresher(
	ctx context.Context,
	identityDomainURL, tokenFile string,
) error {
	unitDir, isUser, err := h.systemdUnitDir()
	if err != nil {
		return err
	}
	values := map[string]any{
		"use_system_level_systemd": !isUser,
		"user_name":                h.username,
		"node_agent_bin":           util.NodeAgentBinaryPath(),
		"identity_domain_url":      identityDomainURL,
		"token_file":               tokenFile,
	}
	for _, u := range []struct{ template, name string }{
		{ociTokenServiceTemplate, ociTokenServiceName},
		{ociTokenTimerTemplate, ociTokenTimerName},
	} {
		if err := h.installSystemdFile(
			ctx, values, u.template, unitDir, u.name, isUser, u.name); err != nil {
			return err
		}
	}
	// The module's systemd helpers tell user from system mode by where the unit file is, so they
	// are called only now that the units are in place. restart, not start: re-arms the timer so a
	// changed unit takes effect on an already-active one.
	return module.ControlSystemdService(ctx, h.username, ociTokenTimerName, "restart", h.logOut)
}

// removeOciRefresher stops and removes the refresher. A safe no-op when it is not installed, so
// teardown can call it unconditionally.
func (h *ConfigureCloudFederation) removeOciRefresher(ctx context.Context) error {
	if !h.ociRefresherPresent() {
		return nil
	}
	unitDir, isUser, err := h.systemdUnitDir()
	if err != nil {
		return err
	}
	var systemPaths []string
	// The timer first, so it cannot start the service again while that is being removed.
	for _, name := range []string{ociTokenTimerName, ociTokenServiceName} {
		path := filepath.Join(unitDir, name)
		if !federationPathExists(path) {
			continue
		}
		// DisableSystemdService deletes the unit file as the node user, which only a user-level unit
		// permits; a system-level one is deleted with sudo below.
		toRemove := ""
		if isUser {
			toRemove = path
		} else {
			systemPaths = append(systemPaths, shellSingleQuote(path))
		}
		if err := module.DisableSystemdService(
			ctx, h.username, name, toRemove, h.logOut); err != nil {
			return err
		}
	}
	if len(systemPaths) == 0 {
		return nil
	}
	_, err = module.RunShellStepsWithRetry(ctx, module.SystemdBackOff, h.username, []struct {
		Desc string
		Cmd  string
	}{
		{"oci-token-remove-units", "sudo rm -f " + strings.Join(systemPaths, " ")},
		{"oci-token-daemon-reload", "sudo systemctl daemon-reload"},
	}, h.logOut)
	return err
}
