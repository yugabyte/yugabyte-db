/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listSupportBundleComponentsUniverseCmd = &cobra.Command{
	Use:     "list-components",
	Aliases: []string{"ls-components"},
	Short:   "List Support Bundle components",
	Long:    "List Support Bundle components",
	Example: `yba universe support-bundle list-components`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		r, response, err := authAPI.ListSupportBundleComponents().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"Universe: Support Bundle",
				"List Components",
			)
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		sbString := util.GetPrintableList(r)

		logrus.Infof(
			"Supported support bundle components: %v\n",
			sbString,
		)

	},
}

func init() {
	listSupportBundleComponentsUniverseCmd.Flags().SortFlags = false
}
