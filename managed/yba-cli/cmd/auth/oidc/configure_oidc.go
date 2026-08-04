/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oidc

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// configureOIDCCmd is used to configure OIDC authentication for YBA
var configureOIDCCmd = &cobra.Command{
	Use:     "configure",
	Aliases: []string{"enable", "create"},
	Short:   "Configure OIDC configuration for YBA",
	Long:    "Configure OIDC configuration for YBA",
	Example: `yba oidc configure --client-id <client-id> --client-secret <client-secret> --discovery-url <discovery-url>`,
	Run: func(cmd *cobra.Command, args []string) {
		oidcProviderMetadataString := util.MaybeGetFlagString(cmd, "provider-configuration")
		if util.IsEmptyString(oidcProviderMetadataString) {
			oidcProviderMetadataFilePath := util.MaybeGetFlagString(
				cmd,
				"provider-configuration-file-path",
			)
			if !util.IsEmptyString(oidcProviderMetadataFilePath) {
				oidcProviderMetadataByte, err := os.ReadFile(oidcProviderMetadataFilePath)
				if err != nil {
					logrus.Fatal(
						formatter.Colorize(
							fmt.Sprintf(
								"Error reading file %s: %s",
								oidcProviderMetadataFilePath,
								err,
							),
							formatter.RedColor,
						),
					)
				}
				oidcProviderMetadataString = string(oidcProviderMetadataByte)
			}
		}
		if !util.IsEmptyString(oidcProviderMetadataString) {
			if err := util.IsValidJSON(oidcProviderMetadataString); err != nil {
				logrus.Fatal(
					formatter.Colorize(
						fmt.Sprintf(
							"Invalid JSON string provided for OIDC provider configuration: %s",
							err,
						),
						formatter.RedColor,
					),
				)
			}
		}
		configureOIDC(
			configureOIDCParams{
				oidcClientID:             util.GetFlagValueAsStringIfSet(cmd, "client-id"),
				oidcClientSecret:         util.GetFlagValueAsStringIfSet(cmd, "client-secret"),
				oidcDiscoveryURL:         util.GetFlagValueAsStringIfSet(cmd, "discovery-url"),
				oidcScope:                util.GetFlagValueAsStringIfSet(cmd, "scope"),
				oidcEmailAttribute:       util.GetFlagValueAsStringIfSet(cmd, "email-attribute"),
				oidcRefreshTokenEndpoint: util.GetFlagValueAsStringIfSet(cmd, "refresh-token-url"),
				oidcProviderMetadata:     oidcProviderMetadataString,
				oidcAutoCreateUser:       util.GetFlagValueAsStringIfSet(cmd, "auto-create-user"),
				oidcDefaultRole:          util.GetFlagValueAsStringIfSet(cmd, "default-role"),
				oidcGroupClaim:           util.GetFlagValueAsStringIfSet(cmd, "group-claim"),
			},
		)
	},
}

func init() {
	configureOIDCCmd.Flags().SortFlags = false
	configureOIDCCmd.Flags().String("client-id", "",
		"OIDC client ID. Required if not already configured")
	configureOIDCCmd.Flags().String("client-secret", "",
		"OIDC client secret associated with the client ID. Required if not already configured.")
	configureOIDCCmd.Flags().String("discovery-url", "",
		"[Optional] URL to the OIDC provider's discovery document. Must be enclosed in double quotes.\n"+
			"           Typically ends with '/.well-known/openid-configuration'.\n"+
			"           Either this or the provider configuration document must be configured.")
	configureOIDCCmd.Flags().String("scope", "openid profile email",
		"[Optional] Space-separated list of scopes to request from the identity provider. Enclosed in quotes.")
	configureOIDCCmd.Flags().String("email-attribute", "",
		"[Optional] Claim name from which to extract the user's email address.")
	configureOIDCCmd.Flags().String("refresh-token-url", "",
		"[Optional] Endpoint used to refresh the access token from the OIDC provider.")

	configureOIDCCmd.Flags().String("provider-configuration", "",
		fmt.Sprintf("[Optional] JSON string representing the full OIDC provider configuration document.\n %s",
			formatter.Colorize("Provide either this or the --provider-configuration-file-path flag.", formatter.GreenColor),
		),
	)
	configureOIDCCmd.Flags().String("provider-configuration-file-path", "",
		fmt.Sprintf("[Optional] Path to the file containing the full OIDC provider configuration document.\n %s",
			formatter.Colorize("Provide either this or the --provider-configuration flag.", formatter.GreenColor),
		),
	)
	configureOIDCCmd.MarkFlagsMutuallyExclusive(
		"provider-configuration",
		"provider-configuration-file-path",
	)

	configureOIDCCmd.Flags().Bool("auto-create-user", true,
		"[Optional] Whether to automatically create a user in YBA if one does not exist.\n"+
			"           If set, a new user will be created upon successful OIDC login.")
	configureOIDCCmd.Flags().String("default-role", "ReadOnly",
		"[Optional] Default role to assign when a role cannot be determined via OIDC.\n"+
			"           Allowed values (case sensitive): ReadOnly, ConnectOnly.")
	configureOIDCCmd.Flags().String("group-claim", "groups",
		"[Optional] Name of the claim in the ID token or user info response that lists user groups.")
}
