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

// editYSQLUniverseCmd represents the universe edit connectionPooling command
var editYSQLUniverseCmd = &cobra.Command{
	Use:     "ysql",
	Aliases: []string{"ysql-configure", "configure-ysql"},
	Short:   "Edit YSQL settings for a YugabyteDB Anywhere Universe",
	Long:    "Edit YSQL settings for a YugabyteDB Anywhere Universe",
	Example: `yba universe edit ysql --name <universe-name> --ysql-password <ysql-password>`,
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

		ysql, err := cmd.Flags().GetString("ysql")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ysql) {
			ysql = strings.ToUpper(ysql)
			if strings.Compare(ysql, util.EnableOpType) != 0 &&
				strings.Compare(ysql, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize("Invalid ysql value\n", formatter.RedColor))
			}
		}

		ysqlAuth, err := cmd.Flags().GetString("ysql-auth")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ysqlAuth) {
			ysqlAuth = strings.ToUpper(ysqlAuth)
			if strings.Compare(ysqlAuth, util.EnableOpType) != 0 &&
				strings.Compare(ysqlAuth, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize("Invalid ysql-auth value\n", formatter.RedColor))
			}
		}
		if strings.Compare(ysqlAuth, util.EnableOpType) == 0 ||
			strings.Compare(ysqlAuth, util.DisableOpType) == 0 {
			ysqlPassword, err := cmd.Flags().GetString("ysql-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(ysqlPassword) {
				logrus.Fatalf(
					formatter.Colorize(
						"YSQL password is required when enabling or disabling auth\n",
						formatter.RedColor,
					),
				)
			}
		}

		connectionPooling, err := cmd.Flags().GetString("connection-pooling")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(connectionPooling) {
			connectionPooling = strings.ToUpper(connectionPooling)
			if strings.Compare(connectionPooling, util.EnableOpType) != 0 &&
				strings.Compare(connectionPooling, util.DisableOpType) != 0 {
				logrus.Fatalf(
					formatter.Colorize("Invalid connection-pooling value\n", formatter.RedColor))
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
			fmt.Sprintf("Are you sure you want to edit YSQL parameters for %s: %s",
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

		req := ybaclient.ConfigureYSQLFormData{}

		userIntent := clusters[0].GetUserIntent()

		ysql, err := cmd.Flags().GetString("ysql")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ysql) {
			ysql = strings.ToUpper(ysql)
			enableYSQL := false
			if strings.Compare(ysql, util.EnableOpType) == 0 {
				enableYSQL = true
			}
			if enableYSQL == userIntent.GetEnableYSQL() {
				logrus.Debugf("Enable YSQL is already set to %t\n", enableYSQL)
			}
			logrus.Debugf("Setting YSQL to %t\n", enableYSQL)
			req.SetEnableYSQL(enableYSQL)
		} else {
			logrus.Debugf("Setting YSQL to %t\n", userIntent.GetEnableYSQL())
			req.SetEnableYSQL(userIntent.GetEnableYSQL())
		}

		ysqlAuth, err := cmd.Flags().GetString("ysql-auth")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(ysqlAuth) {
			ysqlAuth = strings.ToUpper(ysqlAuth)
			enableYSQLAuth := false
			if strings.Compare(ysqlAuth, util.EnableOpType) == 0 {
				enableYSQLAuth = true
			}
			ysqlPassword, err := cmd.Flags().GetString("ysql-password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(ysqlPassword) {
				req.SetYsqlPassword(ysqlPassword)
			}
			if enableYSQLAuth == userIntent.GetEnableYSQLAuth() {
				logrus.Debugf("Enable YSQL auth is already set to %t\n", enableYSQLAuth)
			}
			logrus.Debugf("Setting YSQL auth to %t\n", enableYSQLAuth)
			req.SetEnableYSQLAuth(enableYSQLAuth)
		} else {
			logrus.Debugf("Setting YSQL auth to %t\n", userIntent.GetEnableYSQLAuth())
			req.SetEnableYSQLAuth(userIntent.GetEnableYSQLAuth())
		}

		connectionPooling, err := cmd.Flags().GetString("connection-pooling")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(connectionPooling) {
			connectionPooling = strings.ToUpper(connectionPooling)
			enableConnectionPooling := false
			if strings.Compare(connectionPooling, util.EnableOpType) == 0 {
				enableConnectionPooling = true
			}
			if enableConnectionPooling == userIntent.GetEnableConnectionPooling() {
				logrus.Debugf(
					"Enable connection-pooling is already set to %t\n",
					enableConnectionPooling,
				)
			}
			logrus.Debugf("Setting connection-pooling to %t\n", enableConnectionPooling)
			req.SetEnableConnectionPooling(enableConnectionPooling)
		} else {
			logrus.Debugf("Setting connection-pooling to %t\n", userIntent.GetEnableConnectionPooling())
			req.SetEnableConnectionPooling(userIntent.GetEnableConnectionPooling())
		}

		// connectionPoolingGFlagsString, err := cmd.Flags().GetString("connection-pooling-gflags")
		// if err != nil {
		// 	logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		// }
		// var connectionPoolingGFlags map[string]string
		// if !util.IsEmptyString(connectionPoolingGFlagsString) {
		// 	if strings.HasPrefix(strings.TrimSpace(connectionPoolingGFlagsString), "{") {
		// 		connectionPoolingGFlags = universeutil.ProcessGFlagsJSONString(
		// 			connectionPoolingGFlagsString, "Connection Pooling")
		// 	} else {
		// 		// Assume YAML format
		// 		connectionPoolingGFlags = universeutil.ProcessGFlagsYAMLString(
		// 			connectionPoolingGFlagsString, "Connection Pooling")
		// 	}
		// }
		// if len(connectionPoolingGFlags) > 0 {
		// 	logrus.Debugf("Setting connection-pooling gflags\n")
		// 	req.SetConnectionPoolingGflags(connectionPoolingGFlags)
		// }

		ports := ybaclient.CommunicationPorts{}

		if cmd.Flags().Changed("ysql-server-http-port") {
			httpPort, err := cmd.Flags().GetInt32("ysql-server-http-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debugf("Setting YSQL Server HTTP Port to %d\n", httpPort)
			ports.SetYsqlServerHttpPort(httpPort)
		}

		if cmd.Flags().Changed("ysql-server-rpc-port") {
			rpcPort, err := cmd.Flags().GetInt32("ysql-server-rpc-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debugf("Setting YSQL Server RPC Port to %d\n", rpcPort)
			ports.SetYsqlServerRpcPort(rpcPort)
		}

		if cmd.Flags().Changed("internal-ysql-server-rpc-port") {
			rpcPort, err := cmd.Flags().GetInt32("internal-ysql-server-rpc-port")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debugf("Setting Internal YSQL Server RPC Port to %d\n", rpcPort)
			ports.SetInternalYsqlServerRpcPort(rpcPort)
		}

		req.SetCommunicationPorts(ports)

		rEdit, response, err := authAPI.ConfigureYSQL(universeUUID).
			ConfigureYsqlFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Edit YSQL")
		}

		logrus.Info(
			fmt.Sprintf("Setting ysql configuration in universe %s (%s)\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rEdit)
	},
}

func init() {
	editYSQLUniverseCmd.Flags().SortFlags = false

	editYSQLUniverseCmd.Flags().String("ysql", "",
		"[Optional] YSQL endpoint. Allowed values: enable, disable.")
	editYSQLUniverseCmd.Flags().String("ysql-auth", "",
		"[Optional] YSQL authentication. Allowed values: enable, disable.")
	editYSQLUniverseCmd.Flags().String("ysql-password", "",
		fmt.Sprintf(
			"[Optional] YSQL authentication password. Use single quotes ('') to provide "+
				"values with special characters. %s",
			formatter.Colorize(
				"Required when YSQL authentication is being enabled or disabled",
				formatter.GreenColor,
			),
		))
	editYSQLUniverseCmd.Flags().String("connection-pooling", "",
		"[Optional] Connection Pooling settings. Allowed values: enable, disable.")
	// editYSQLUniverseCmd.Flags().String("connection-pooling-gflags", "",
	// 	"[Optional] Connection Pooling GFlags in map (JSON or YAML) format. "+
	// 		"Provide the gflags in the following formats: "+
	// 		"\"--connection-pooling-gflags {\"connection-pooling-gflag-key-1\":\"value-1\","+
	// 		"\"connection-pooling-gflag-key-2\":\"value-2\" }\" or"+
	// 		"  \"--connection-pooling-gflags "+
	// 		"\"connection-pooling-gflag-key-1: value-1\nconnection-pooling-gflag-key-2"+
	// 		": value-2\nconnection-pooling-gflag-key-3: value-3\".")
	editYSQLUniverseCmd.Flags().Int("ysql-server-http-port", 13000,
		"[Optional] YSQL Server HTTP Port.")
	editYSQLUniverseCmd.Flags().Int("ysql-server-rpc-port", 5433,
		"[Optional] YSQL Server RPC Port.")
	editYSQLUniverseCmd.Flags().Int("internal-ysql-server-rpc-port", 6433,
		"[Optional] Internal YSQL Server RPC Port used when connection pooling is enabled.")
}
