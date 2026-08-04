/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

var listUniverseCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere universes",
	Long:    "List YugabyteDB Anywhere universes",
	Example: `yba universe list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeListRequest := authAPI.ListUniverses()
		// filter by name and/or by universe code
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if universeName != "" {
			universeListRequest = universeListRequest.Name(universeName)
		}

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "List")
		}

		universeCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  universe.NewUniverseFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No universes found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		universe.Write(universeCtx, r)

	},
}

func init() {
	listUniverseCmd.Flags().SortFlags = false

	listUniverseCmd.Flags().StringP("name", "n", "", "[Optional] Name of the universe.")
}
