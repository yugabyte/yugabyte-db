/*
 * Copyright (c) YugabyteDB, Inc.
 */

package edit

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// editYCQLUniverseCmd represents the universe edit connectionPooling command
var editYCQLUniverseCmd = &cobra.Command{
	Use:     "ycql",
	Aliases: []string{"ycql-configure", "configure-ycql"},
	Short:   "Edit YCQL settings for a YugabyteDB Anywhere Universe",
	Long:    "Edit YCQL settings for a YugabyteDB Anywhere Universe",
	Example: `yba universe edit ycql --name <universe-name> --ycql-password <ycql-password>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to edit\n", formatter.RedColor))
		}

		ycql, err := cmd.Flags().GetString("ycql")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ycql) {
			ycql = strings.ToUpper(ycql)
			if strings.Compare(ycql, util.EnableOpType) != 0 &&
				strings.Compare(ycql, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize("Invalid ycql value\n", formatter.RedColor))
			}
		}

		ycqlAuth, err := cmd.Flags().GetString("ycql-auth")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ycqlAuth) {
			ycqlAuth = strings.ToUpper(ycqlAuth)
			if strings.Compare(ycqlAuth, util.EnableOpType) != 0 &&
				strings.Compare(ycqlAuth, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize("Invalid ycql-auth value\n", formatter.RedColor))
			}
		}
		if strings.Compare(ycqlAuth, util.EnableOpType) == 0 ||
			strings.Compare(ycqlAuth, util.DisableOpType) == 0 {
			ycqlPassword, err := cmd.Flags().GetString("ycql-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(ycqlPassword) {
				logrus.Fatalf(
					formatter.Colorize(
						"YCQL password is required when enabling or disabling auth\n",
						formatter.RedColor,
					),
				)
			}
		}

		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to edit YCQL parameters for %s: %s",
				util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.EditOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			logrus.Fatalln(
				formatter.Colorize(
					"No clusters found in universe "+
						universeName+" ("+universeUUID+")\n",
					formatter.RedColor),
			)
		}

		req := ybaclient.ConfigureYCQLFormData{}

		userIntent := clusters[0].GetUserIntent()

		ycql, err := cmd.Flags().GetString("ycql")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ycql) {
			ycql = strings.ToUpper(ycql)
			enableYCQL := false
			if strings.Compare(ycql, util.EnableOpType) == 0 {
				enableYCQL = true
			}
			if enableYCQL == userIntent.GetEnableYCQL() {
				logrus.Debugf("Enable YCQL is already set to %t\n", enableYCQL)
			}
			logrus.Debugf("Setting YCQL to %t\n", enableYCQL)
			req.SetEnableYCQL(enableYCQL)
		} else {
			logrus.Debugf("Setting YCQL to %t\n", userIntent.GetEnableYCQL())
			req.SetEnableYCQL(userIntent.GetEnableYCQL())
		}

		ycqlAuth, err := cmd.Flags().GetString("ycql-auth")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ycqlAuth) {
			ycqlAuth = strings.ToUpper(ycqlAuth)
			enableYCQLAuth := false
			if strings.Compare(ycqlAuth, util.EnableOpType) == 0 {
				enableYCQLAuth = true
			}
			ycqlPassword, err := cmd.Flags().GetString("ycql-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(ycqlPassword) {
				req.SetYcqlPassword(ycqlPassword)
			}
			if enableYCQLAuth == userIntent.GetEnableYCQLAuth() {
				logrus.Debugf("Enable YCQL auth is already set to %t\n", enableYCQLAuth)
			}
			logrus.Debugf("Setting YCQL auth to %t\n", enableYCQLAuth)
			req.SetEnableYCQLAuth(enableYCQLAuth)
		} else {
			logrus.Debugf("Setting YCQL auth to %t\n", userIntent.GetEnableYCQLAuth())
			req.SetEnableYCQLAuth(userIntent.GetEnableYCQLAuth())
		}

		ports := ybaclient.CommunicationPorts{}

		if cmd.Flags().Changed("ycql-server-http-port") {
			httpPort, err := cmd.Flags().GetInt32("ycql-server-http-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debugf("Setting YCQL Server HTTP Port to %d\n", httpPort)
			ports.SetYqlServerHttpPort(httpPort)
		}

		if cmd.Flags().Changed("ycql-server-rpc-port") {
			rpcPort, err := cmd.Flags().GetInt32("ycql-server-rpc-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debugf("Setting YCQL Server RPC Port to %d\n", rpcPort)
			ports.SetYqlServerRpcPort(rpcPort)
		}

		req.SetCommunicationPorts(ports)

		rEdit, response, err := authAPI.ConfigureYCQL(universeUUID).
			ConfigureYcqlFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Edit YCQL")
		}

		logrus.Info(
			fmt.Sprintf("Setting ycql configuration in universe %s (%s)\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rEdit)
	},
}

func init() {
	editYCQLUniverseCmd.Flags().SortFlags = false

	editYCQLUniverseCmd.Flags().String("ycql", "",
		"[Optional] YCQL endpoint. Allowed values: enable, disable.")
	editYCQLUniverseCmd.Flags().String("ycql-auth", "",
		"[Optional] YCQL authentication. Allowed values: enable, disable.")
	editYCQLUniverseCmd.Flags().String("ycql-password", "",
		fmt.Sprintf(
			"[Optional] YCQL authentication password. Use single quotes ('') to provide "+
				"values with special characters. %s",
			formatter.Colorize(
				"Required when YCQL authentication is being enabled or disabled",
				formatter.GreenColor,
			),
		))
	editYCQLUniverseCmd.Flags().Int("ycql-server-http-port", 13000,
		"[Optional] YCQL Server HTTP Port.")
	editYCQLUniverseCmd.Flags().Int("ycql-server-rpc-port", 5433,
		"[Optional] YCQL Server RPC Port.")
}
