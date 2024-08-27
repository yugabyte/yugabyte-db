/*
 * Copyright (c) YugaByte, Inc.
 */

package eitutil

import (
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

// ListEITUtil executes the list eit command
func ListEITUtil(cmd *cobra.Command, commandCall, eitCertType string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	certs, response, err := authAPI.GetListOfCertificates().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"EIT", "List")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	var rName []ybaclient.CertificateInfoExt
	eitName, _ := cmd.Flags().GetString("name")
	if strings.TrimSpace(eitName) != "" {
		for _, c := range certs {
			if strings.Compare(c.GetLabel(), eitName) == 0 {
				rName = append(rName, c)
			}
		}
	} else {
		rName = certs
	}

	var r []ybaclient.CertificateInfoExt
	if len(strings.TrimSpace(eitCertType)) != 0 {
		for _, c := range rName {
			if strings.Compare(c.GetCertType(), eitCertType) == 0 {
				r = append(r, c)
			}
		}
	} else {
		r = rName
	}

	eitCtx := formatter.Context{
		Output: os.Stdout,
		Format: eit.NewEITFormat(viper.GetString("output")),
	}
	if len(r) < 1 {
		if util.IsOutputType("table") {
			logrus.Infoln("No configurations found\n")
		} else {
			logrus.Infoln("[]\n")
		}
		return
	}
	eit.Write(eitCtx, r)
}
