/*
 * Copyright (c) YugaByte, Inc.
 */

package oidc

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/scope/key"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/oidc"
)

type configureOIDCParams struct {
	oidcClientID             string
	oidcClientSecret         string
	oidcDiscoveryURL         string
	oidcScope                string
	oidcEmailAttribute       string
	oidcRefreshTokenEndpoint string
	oidcProviderMetadata     string
	oidcAutoCreateUser       string
	oidcDefaultRole          string
	oidcGroupClaim           string
}

func configureOIDC(params configureOIDCParams) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	key.CheckAndSetGlobalKey(authAPI, util.ToggleOIDCKey, "true")
	key.CheckAndSetGlobalKey(authAPI, util.SecurityTypeKey, util.OIDCGroupMappingType)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCClientIDKey, params.oidcClientID)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCClientSecretKey, params.oidcClientSecret)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCDiscoveryURLKey, params.oidcDiscoveryURL)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCScopeKey, params.oidcScope)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCEmailAttributeKey, params.oidcEmailAttribute)
	key.CheckAndSetGlobalKey(
		authAPI,
		util.OIDCRefreshTokenEndpointKey,
		params.oidcRefreshTokenEndpoint,
	)
	if params.oidcProviderMetadata != "" {
		key.CheckAndSetGlobalKey(authAPI, util.OIDCProviderMetadataKey, fmt.Sprintf(
			"\"\"\"%s\"\"\"", params.oidcProviderMetadata))
	}
	key.CheckAndSetGlobalKey(authAPI, util.OIDCAutoCreateUserKey, params.oidcAutoCreateUser)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCDefaultRoleKey, params.oidcDefaultRole)
	key.CheckAndSetGlobalKey(authAPI, util.OIDCGroupClaimKey, params.oidcGroupClaim)
	logrus.Info(
		formatter.Colorize("OIDC configuration updated successfully.\n", formatter.GreenColor))
	oidcConfig := getScopedConfigWithOIDCKeys(authAPI, true /*inherited*/)
	writeOIDCConfig(oidcConfig)
	validateOIDCConfig(oidcConfig)
}

func disableOIDC(resetAll bool, keysToReset map[string]bool) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	oidcConfig := getScopedConfigWithOIDCKeys(authAPI, false /*inherited*/)
	// Delete the toggle key if present in the config
	if len(oidcConfig) == 0 {
		logrus.Warn(formatter.Colorize("No OIDC configuration found.\n", formatter.YellowColor))
		return
	}
	if resetAll {
		for _, keyConfig := range oidcConfig {
			logrus.Info(
				formatter.Colorize(
					fmt.Sprintf("Deleting key: %s\n", util.OidcKeyToFlagMap[keyConfig.GetKey()]),
					formatter.GreenColor,
				),
			)
			key.DeleteGlobalKey(authAPI, keyConfig.GetKey())
		}
	} else {
		for _, keyConfig := range oidcConfig {
			if _, exists := keysToReset[keyConfig.GetKey()]; exists {
				logrus.Info(
					formatter.Colorize(
						fmt.Sprintf("Deleting key: %s\n", util.OidcKeyToFlagMap[keyConfig.GetKey()]),
						formatter.GreenColor,
					),
				)
				key.DeleteGlobalKey(authAPI, keyConfig.GetKey())
			}
		}
	}
	logrus.Info(
		formatter.Colorize("OIDC configuration deleted successfully.\n", formatter.GreenColor))
}

func getOIDCConfig(inherited bool) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	oidcConfig := getScopedConfigWithOIDCKeys(authAPI, inherited)
	if len(oidcConfig) == 0 {
		logrus.Info(formatter.Colorize("No OIDC configuration found.\n", formatter.YellowColor))
		return
	}
	writeOIDCConfig(oidcConfig)
}

func writeOIDCConfig(oidcConfig []ybaclient.ConfigEntry) {
	logrus.Info(formatter.Colorize("OIDC configuration:\n", formatter.GreenColor))
	oidcConfigCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  oidc.NewOIDCFormat(viper.GetString("output")),
	}
	oidc.Write(oidcConfigCtx, oidcConfig)
}

func getScopedConfigWithOIDCKeys(
	authAPI *ybaAuthClient.AuthAPIClient,
	inherited bool,
) []ybaclient.ConfigEntry {
	r, response, err := authAPI.GetConfig(util.GlobalScopeUUID).
		IncludeInherited(inherited).
		Execute()
	if err != nil {
		logrus.Fatal(formatter.Colorize(
			util.ErrorFromHTTPResponse(response, err, "OIDC config", "Get").Error()+"\n",
			formatter.RedColor))
	}

	// Filter and return only OIDC-related keys
	return filterOIDCKeys(r.GetConfigEntries())
}

func filterOIDCKeys(configEntries []ybaclient.ConfigEntry) []ybaclient.ConfigEntry {
	oidcKeys := make([]ybaclient.ConfigEntry, 0, len(configEntries))
	for _, keyConfig := range configEntries {
		if util.IsOIDCKey(keyConfig.GetKey()) {
			oidcKeys = append(oidcKeys, keyConfig)
		}
	}
	return oidcKeys
}

func validateOIDCConfig(oidcConfig []ybaclient.ConfigEntry) {
	requiredKeys := map[string]string{
		util.OIDCClientIDKey:     "OIDC Client ID is not set.\n",
		util.OIDCClientSecretKey: "OIDC Client Secret not set.\n",
	}

	for _, keyConfig := range oidcConfig {
		if message, exists := requiredKeys[keyConfig.GetKey()]; exists &&
			keyConfig.GetValue() == "" {
			logrus.Error(formatter.Colorize(message, formatter.RedColor))
		}
	}

}
