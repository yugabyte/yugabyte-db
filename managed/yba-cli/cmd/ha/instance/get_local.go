/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instance

import (
	"encoding/json"
	"io"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ha"
)

var getLocalHAInstanceCmd = &cobra.Command{
	Use:     "get-local",
	Aliases: []string{"local"},
	Short:   "Get the local HA platform instance",
	Long:    "Get the local platform instance for an HA configuration",
	Example: `yba ha instance get-local --uuid <uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(configUUID) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Config UUID is required\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		configUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		// this doesn't return an instance, just the http response, so it doesn't really return the local ?
		response, err := authAPI.GetLocalHAInstance(configUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "HA Instance", "Get Local")
		}
		defer response.Body.Close()
		body, err := io.ReadAll(response.Body)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize("Error reading response: "+err.Error()+"\n", formatter.RedColor),
			)
		}

		var instance map[string]interface{}
		if err := json.Unmarshal(body, &instance); err != nil {
			logrus.Fatalf(
				formatter.Colorize("Error parsing response: "+err.Error()+"\n", formatter.RedColor),
			)
		}
		haCtx := formatter.Context{
			Command: "get-local",
			Output:  os.Stdout,
			Format:  ha.NewInstancesFormat(viper.GetString("output")),
		}
		ha.WriteInstance(haCtx, instance)
	},
}

func init() {
	getLocalHAInstanceCmd.Flags().SortFlags = false
	getLocalHAInstanceCmd.Flags().String("uuid", "",
		"[Required] UUID of the HA configuration")
	getLocalHAInstanceCmd.MarkFlagRequired("uuid")

}
