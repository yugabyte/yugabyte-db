/*
 * Copyright (c) YugaByte, Inc.
 */

package eitutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/eit"
)

// CreateEITValidation validates the delete config command
func CreateEITValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption in transit config name found to create\n",
				formatter.RedColor))
	}
}

// CreateEITUtil is a util task for create eit
func CreateEITUtil(
	authAPI *ybaAuthClient.AuthAPIClient,
	eitName, eitCertType string,
	requestBody ybaclient.CertificateParams) {
	callSite := fmt.Sprintf("EIT: %s", eitCertType)

	eitUUID, response, err := authAPI.Upload().Certificate(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Create")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if len(strings.TrimSpace(eitUUID)) == 0 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while adding encryption in transit configration %s\n",
				eitName),
			formatter.RedColor))
	}

	logrus.Infof(
		"Successfully added encryption in transit configration %s (%s)\n",
		formatter.Colorize(eitName, formatter.GreenColor), eitUUID)

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"EIT", "List")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	var r []ybaclient.CertificateInfoExt
	for _, c := range certs {
		if strings.Compare(c.GetCertType(), eitCertType) == 0 &&
			strings.Compare(c.GetLabel(), eitName) == 0 {
			r = append(r, c)
		}
	}

	eitCtx := formatter.Context{
		Command: "create",
		Output:  os.Stdout,
		Format:  eit.NewEITFormat(viper.GetString("output")),
	}
	if len(r) < 1 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"An error occurred while adding encryption in transit configration %s (%s)\n",
				eitName, eitUUID),
			formatter.RedColor))
	}
	eit.Write(eitCtx, r)

}
