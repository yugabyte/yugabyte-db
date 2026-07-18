/*
 * Copyright (c) YugabyteDB, Inc.
 */

package template

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/template"
)

var listTemplateAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere alert policy templates",
	Long:    "List YugabyteDB Anywhere alert policy templates",
	Example: `yba alert policy template list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		templateListRequest := authAPI.ListAlertTemplates()
		// filter by name and/or by template code
		templateName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		targetType, err := cmd.Flags().GetString("target-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertFilter := ybaclient.AlertTemplateApiFilter{
			Name:       util.GetStringPointer(templateName),
			TargetType: util.GetStringPointer(strings.ToUpper(targetType)),
		}

		templateListRequest = templateListRequest.ListTemplatesRequest(alertFilter)

		r, response, err := templateListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Template", "List")
		}

		templateCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  template.NewAlertTemplateFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No alert templates found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		template.Write(templateCtx, r)

	},
}

func init() {
	listTemplateAlertCmd.Flags().SortFlags = false

	listTemplateAlertCmd.Flags().StringP("name", "n", "", "[Optional] Name of the alert template.")
	listTemplateAlertCmd.Flags().String("target-type", "",
		"[Optional] Target type of the alert template. Allowed values: platform, universe.")
}
