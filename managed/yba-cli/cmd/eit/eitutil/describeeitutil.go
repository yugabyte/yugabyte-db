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

// DescribeEITValidation validates the describe config command
func DescribeEITValidation(cmd *cobra.Command) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(configNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption in transit config name found to describe\n",
				formatter.RedColor))
	}
}

// DescribeEITUtil executes the describe eit command
func DescribeEITUtil(cmd *cobra.Command, commandCall, certType string) {
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
			formatter.Colorize("No configuration name found to describe\n", formatter.RedColor))
	}

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		callSite := "EIT"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "Describe")
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

	if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullEITContext := *eit.NewFullEITContext()
		fullEITContext.Output = os.Stdout
		fullEITContext.Format = eit.NewFullEITFormat(viper.GetString("output"))
		fullEITContext.SetFullEIT(r[0])
		fullEITContext.Write()
		return
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No configurations with name: %s found\n", eitName),
				formatter.RedColor,
			))
	}

	eitCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  eit.NewEITFormat(viper.GetString("output")),
	}
	eit.Write(eitCtx, r)
}
