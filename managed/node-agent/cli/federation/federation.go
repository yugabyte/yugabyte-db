// Copyright (c) YugabyteDB, Inc.

package federation

import (
	"errors"
	"fmt"
	"node-agent/app/federation/ocitoken"
	"node-agent/backoff"
	"node-agent/util"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/spf13/cobra"
)

const (
	exchangeAttempts   = 3
	exchangeRetryDelay = 10 * time.Second
)

var (
	// Hidden: these exist for the systemd unit that node-agent installs, not for operators.
	federationCmd = &cobra.Command{
		Use:    "federation ...",
		Short:  "Internal commands for cross-cloud federated IAM",
		Hidden: true,
	}

	ociTokenCmd = &cobra.Command{
		Use:   "oci-token",
		Short: "Refresh the OCI identity token used for cross-cloud federation",
		Long: "Exchanges this node's OCI instance principal for an identity-domain JWT and writes it " +
			"to a file for the GCP external_account credential to read. Run periodically by the " +
			"yb-federation-oci-token systemd timer.",
		Args: cobra.NoArgs,
		RunE: ociTokenHandler,
	}
)

// SetupFederationCommand registers the federation commands on the root command.
func SetupFederationCommand(parentCmd *cobra.Command) {
	ociTokenCmd.Flags().String("identity-domain-url", "", "Base URL of the OCI identity domain")
	ociTokenCmd.Flags().String("output", "", "Absolute path of the token file to write")
	ociTokenCmd.Flags().Duration(
		"min-lifetime", 0, "Fail if the issued token is valid for less than this (0 disables)")
	_ = ociTokenCmd.MarkFlagRequired("identity-domain-url")
	_ = ociTokenCmd.MarkFlagRequired("output")
	federationCmd.AddCommand(ociTokenCmd)
	parentCmd.AddCommand(federationCmd)
}

// ociTokenHandler also writes a failure to stderr: the federation task shows only a failed step's
// stderr, while the root command reports errors on stdout.
func ociTokenHandler(cmd *cobra.Command, args []string) error {
	err := refreshOciToken(cmd)
	if err != nil {
		fmt.Fprintln(cmd.ErrOrStderr(), err)
	}
	return err
}

func refreshOciToken(cmd *cobra.Command) error {
	// The root command runs without a cancellable context, so stopping the unit or the setup step
	// cancels this one, which ends a pending exchange or retry wait.
	ctx, stop := signal.NotifyContext(cmd.Context(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	identityDomainURL, err := cmd.Flags().GetString("identity-domain-url")
	if err != nil {
		return err
	}
	output, err := cmd.Flags().GetString("output")
	if err != nil {
		return err
	}
	minLifetime, err := cmd.Flags().GetDuration("min-lifetime")
	if err != nil {
		return err
	}
	// Validated before anything is signed or sent: the URL receives a request carrying this node's
	// credentials, and the output path is written to.
	endpoint, err := ocitoken.TokenEndpoint(identityDomainURL)
	if err != nil {
		return err
	}
	if !filepath.IsAbs(output) {
		return errors.New("output must be an absolute path")
	}

	signer, err := ocitoken.InstancePrincipalSigner()
	if err != nil {
		return err
	}
	// Retried so a transient error does not cost a whole refresh cycle; a wrong domain fails the same
	// way each time, so setup learns of it a little later.
	var token string
	err = backoff.Do(ctx, backoff.NewSimpleBackOff(exchangeRetryDelay, exchangeAttempts),
		func(int) error {
			var e error
			token, e = ocitoken.Exchange(ctx, ocitoken.NewClient(), signer, endpoint)
			return e
		})
	if err != nil {
		return err
	}
	if minLifetime > 0 {
		if err := ocitoken.RequireLifetime(token, minLifetime, time.Now()); err != nil {
			return err
		}
	}
	// Replaced atomically, so YBC reloading its credential never reads a half-written token. The
	// empty user leaves the file owned by whoever runs this unit.
	if err := util.WriteFileIfChanged(output, token, 0600, ""); err != nil {
		return fmt.Errorf("could not write the token file: %w", err)
	}
	// Never print the token: this output is captured by systemd and by the federation task.
	fmt.Fprintln(cmd.OutOrStdout(), "OCI identity token refreshed")
	return nil
}
