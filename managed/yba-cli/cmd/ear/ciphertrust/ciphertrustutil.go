/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ciphertrust

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

func runCreateCipherTrustEAR(cmd *cobra.Command) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	configName := util.MustGetFlagString(cmd, "name")
	managerURL := util.MustGetFlagString(cmd, "manager-url")
	authType := strings.ToUpper(util.MustGetFlagString(cmd, "auth-type"))
	username := util.MaybeGetFlagString(cmd, "username")
	password := util.MaybeGetFlagString(cmd, "password")
	refreshToken := util.MaybeGetFlagString(cmd, "refresh-token")
	keyName := util.MustGetFlagString(cmd, "key-name")
	keyAlgorithm := strings.ToUpper(util.MaybeGetFlagString(cmd, "key-algorithm"))
	keySize := util.MustGetFlagInt(cmd, "key-size")

	requestBody := map[string]interface{}{
		"name":                            configName,
		util.CipherTrustManagerURLField:   managerURL,
		util.CipherTrustAuthTypeField:     authType,
		util.CipherTrustKeyNameField:      keyName,
		util.CipherTrustKeyAlgorithmField: keyAlgorithm,
		util.CipherTrustKeySizeField:      keySize,
	}

	if authType == "PASSWORD" {
		requestBody[util.CipherTrustUsernameField] = username
		requestBody[util.CipherTrustPasswordField] = password
	} else if authType == "REFRESH_TOKEN" {
		requestBody[util.CipherTrustRefreshTokenField] = refreshToken
	}

	rTask, response, err := authAPI.CreateKMSConfig(util.CipherTrustEARType).
		KMSConfig(requestBody).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, "EAR: CipherTrust", "Create")
	}

	earutil.WaitForCreateEARTask(authAPI, configName, rTask, util.CipherTrustEARType)
}

func runUpdateCipherTrustEAR(cmd *cobra.Command) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	configName := util.MustGetFlagString(cmd, "name")
	config, err := earutil.GetEARConfig(authAPI, configName, util.CipherTrustEARType)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	authType := strings.ToUpper(util.MustGetFlagString(cmd, "auth-type"))
	requestBody := map[string]interface{}{}
	hasUpdates := false

	requestBody[util.CipherTrustAuthTypeField] = authType

	if authType == "PASSWORD" {
		hasUpdates = true
		requestBody[util.CipherTrustUsernameField] = util.MustGetFlagString(cmd, "username")
		requestBody[util.CipherTrustPasswordField] = util.MustGetFlagString(cmd, "password")
	}
	if authType == "REFRESH_TOKEN" {
		hasUpdates = true
		requestBody[util.CipherTrustRefreshTokenField] = util.MustGetFlagString(
			cmd,
			"refresh-token",
		)
	}

	if hasUpdates {
		earutil.UpdateEARConfig(
			authAPI,
			configName,
			config.ConfigUUID,
			util.CipherTrustEARType,
			requestBody)
		return
	}
	logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
}
