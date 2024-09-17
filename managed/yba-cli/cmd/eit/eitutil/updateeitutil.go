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

// UpdateEITValidation validates the update config command
func UpdateEITValidation(cmd *cobra.Command) {
	viper.BindPFlag("force", cmd.Flags().Lookup("force"))
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption in transit config name found to update\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to update %s: %s", "encryption in transit configuration",
			configNameFlag),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// GetEITConfig fetches config
func GetEITConfig(
	authAPI *ybaAuthClient.AuthAPIClient,
	eitName, certType, commandCall string) ybaclient.CertificateInfoExt {
	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		callSite := "EIT"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(
			response, err, callSite, "Update - Get Certificates")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	var r []ybaclient.CertificateInfoExt
	if strings.TrimSpace(eitName) != "" {
		for _, c := range certs {
			if strings.Compare(c.GetLabel(), eitName) == 0 {
				if certType != "" {
					if strings.Compare(c.GetCertType(), certType) == 0 {
						r = append(r, c)
					}
				} else {
					r = append(r, c)
				}

			}
		}
	}

	if len(r) < 1 {
		errMessage := ""
		if len(strings.TrimSpace(certType)) == 0 {
			errMessage = fmt.Sprintf("No configurations with name: %s found\n", eitName)
		} else {
			errMessage = fmt.Sprintf(
				"No configurations with name: %s and type: %s found\n", eitName, certType)
		}
		logrus.Fatalf(
			formatter.Colorize(
				errMessage,
				formatter.RedColor,
			))
	}

	return r[0]
}

// UpdateEITUtil is a util task for update eit
func UpdateEITUtil(
	authAPI *ybaAuthClient.AuthAPIClient,
	eitName, eitUUID, eitCertType string,
	requestBody ybaclient.CertificateParams) {
	callSite := fmt.Sprintf("EIT: %s", eitCertType)

	rUpdate, response, err := authAPI.EditCertificate(eitUUID).Certificate(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Update")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if !rUpdate.GetSuccess() {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while updating encryption in transit configration %s (%s)\n",
					formatter.Colorize(eitName, formatter.GreenColor), eitUUID),
				formatter.RedColor))
	}
	logrus.Info(fmt.Sprintf("The encryption in transit configration %s (%s) has been updated\n",
		formatter.Colorize(eitName, formatter.GreenColor), eitUUID))

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
		Output: os.Stdout,
		Format: eit.NewEITFormat(viper.GetString("output")),
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
