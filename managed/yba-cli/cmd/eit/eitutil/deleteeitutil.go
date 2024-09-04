/*
 * Copyright (c) YugaByte, Inc.
 */

package eitutil

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// DeleteEITValidation validates the delete config command
func DeleteEITValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption in transit config name found to delete\n",
				formatter.RedColor))
	}
	err = util.ConfirmCommand(
		fmt.Sprintf(
			"Are you sure you want to delete %s: %s", "encryption in transit configuration",
			configNameFlag),
		viper.GetBool("force"))
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}

// DeleteEITUtil executes the delete eit command
func DeleteEITUtil(cmd *cobra.Command, commandCall, certType string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	eitNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var eitName string
	if len(eitNameFlag) > 0 {
		eitName = eitNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No configuration name found to delete\n", formatter.RedColor))
	}

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		callSite := "EIT"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(
			response, err, callSite, "Delete - Get Certificates")
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

	eitUUID := r[0].GetUuid()
	rDelete, response, err := authAPI.DeleteCertificate(eitUUID).Execute()
	if err != nil {
		callSite := "EIT"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Delete")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	if rDelete.GetSuccess() {
		msg := fmt.Sprintf("The configuration %s (%s) has been deleted",
			formatter.Colorize(eitName, formatter.GreenColor), eitUUID)

		logrus.Infoln(msg + "\n")
	} else {
		logrus.Errorf(
			formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while removing configration %s (%s)\n",
					eitName, eitUUID),
				formatter.RedColor))
	}
}
