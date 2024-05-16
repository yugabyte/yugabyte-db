/*
 * Copyright (c) YugaByte, Inc.
 */

package universe

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

var describeUniverseCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe",
	Long:    "Describe a universe in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		universeListRequest := authAPI.ListUniverses()
		universeName, _ := cmd.Flags().GetString("name")
		universeListRequest = universeListRequest.Name(universeName)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		universe.Certificates, response, err = authAPI.GetListOfCertificates().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Describe - Get Certificates")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		universe.KMSConfigs, response, err = authAPI.ListKMSConfigs().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Describe - Get KMS Configurations")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) > 0 && viper.GetString("output") == "table" {
			fullUniverseContext := *universe.NewFullUniverseContext()
			fullUniverseContext.Output = os.Stdout
			fullUniverseContext.Format = universe.NewFullUniverseFormat(viper.GetString("output"))
			fullUniverseContext.SetFullUniverse(r[0])
			fullUniverseContext.Write()
			return
		}

		if len(r) < 1 {
			fmt.Println("No universes found")
			return
		}

		universeCtx := formatter.Context{
			Output: os.Stdout,
			Format: universe.NewUniverseFormat(viper.GetString("output")),
		}
		universe.Write(universeCtx, r)

	},
}

func init() {
	describeUniverseCmd.Flags().SortFlags = false
	describeUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be created.")
	describeUniverseCmd.MarkFlagRequired("name")
}
