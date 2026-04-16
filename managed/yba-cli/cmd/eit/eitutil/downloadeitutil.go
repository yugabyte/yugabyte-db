/*
 * Copyright (c) YugabyteDB, Inc.
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/eit/eitdownloadclient"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/eit/eitdownloadroot"
)

// DownloadEITValidation executes the download eit command
func DownloadEITValidation(cmd *cobra.Command, commandType string) {
	configNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if util.IsEmptyString(configNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize(
				"No encryption in transit config name found to download certificate\n",
				formatter.RedColor))
	}
	if strings.Compare(commandType, "client") == 0 {
		username, err := cmd.Flags().GetString("username")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if util.IsEmptyString(username) {
			logrus.Fatalln(
				formatter.Colorize("No username found to download\n", formatter.RedColor))
		}
	}
}

// DownloadRootEITUtil executes the download root eit command
func DownloadRootEITUtil(cmd *cobra.Command, commandCall, certType string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	callSite := "EIT"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	eitNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var eitName string
	if len(eitNameFlag) > 0 {
		eitName = eitNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No configuration name found to download\n", formatter.RedColor))
	}

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Download Root Cert - Get Certificates")
	}

	var r []ybaclient.CertificateInfoExt
	if !util.IsEmptyString(eitName) {
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
		if !util.IsEmptyString(certType) {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No configurations with name: %s and type: %s found\n",
						eitName,
						certType),
					formatter.RedColor,
				))
		}
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No configurations with name: %s found\n", eitName),
				formatter.RedColor,
			))
	}

	certUUID := r[0].GetUuid()

	download, response, err := authAPI.GetRootCert(certUUID).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Download Root Cert")
	}

	downloads := make([]map[string]interface{}, 0)
	downloads = append(downloads, download)

	if len(downloads) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullEITDownloadContext := *eitdownloadroot.NewFullEITDownloadRootContext()
		fullEITDownloadContext.Output = os.Stdout
		fullEITDownloadContext.Format = eitdownloadroot.NewFullEITDownloadRootFormat(
			viper.GetString("output"),
		)
		fullEITDownloadContext.SetFullEITDownloadRoot(downloads[0])
		fullEITDownloadContext.Write()
		return
	}

	eitDownloadCtx := formatter.Context{
		Command: "download",
		Output:  os.Stdout,
		Format:  eitdownloadroot.NewEITDownloadRootFormat(viper.GetString("output")),
	}
	eitdownloadroot.Write(eitDownloadCtx, downloads)
}

// DownloadClientEITUtil executes the download root eit command
func DownloadClientEITUtil(cmd *cobra.Command, commandCall, certType string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	callSite := "EIT"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	eitNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	var eitName string
	if len(eitNameFlag) > 0 {
		eitName = eitNameFlag
	} else {
		logrus.Fatalln(
			formatter.Colorize("No configuration name found to download\n", formatter.RedColor))
	}

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Download Client Cert - Get Certificates")
	}

	var r []ybaclient.CertificateInfoExt
	if !util.IsEmptyString(eitName) {
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
		if !util.IsEmptyString(certType) {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No configurations with name: %s and type: %s found\n",
						eitName,
						certType),
					formatter.RedColor,
				))
		}
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No configurations with name: %s found\n", eitName),
				formatter.RedColor,
			))
	}

	certUUID := r[0].GetUuid()

	username, err := cmd.Flags().GetString("username")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	req := ybaclient.ClientCertParams{
		Username: username,
	}

	download, response, err := authAPI.GetClientCert(certUUID).Certificate(req).Execute()
	if err != nil {
		util.FatalHTTPError(response, err, callSite, "Download Client Cert")
	}

	downloads := util.CheckAndAppend(
		make([]ybaclient.CertificateDetails, 0),
		download,
		"No client certificate found",
	)

	if len(downloads) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullEITDownloadContext := *eitdownloadclient.NewFullEITDownloadClientContext()
		fullEITDownloadContext.Output = os.Stdout
		fullEITDownloadContext.Format = eitdownloadclient.NewFullEITDownloadClientFormat(
			viper.GetString("output"),
		)
		fullEITDownloadContext.SetFullEITDownloadClient(downloads[0])
		fullEITDownloadContext.Write()
		return
	}

	eitDownloadCtx := formatter.Context{
		Command: "download",
		Output:  os.Stdout,
		Format:  eitdownloadclient.NewEITDownloadClientFormat(viper.GetString("output")),
	}
	eitdownloadclient.Write(eitDownloadCtx, downloads)
}
