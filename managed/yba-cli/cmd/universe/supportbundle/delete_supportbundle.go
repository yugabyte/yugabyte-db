/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteSupportBundleUniverseCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"rm", "remove"},
	Short:   "Delete a support bundle for a YugabyteDB Anywhere universe",
	Long:    "Delete a support bundle for a YugabyteDB Anywhere universe",
	Example: `yba universe support-bundle delete --name <universe-name> --uuid <support-bundle-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName := util.MustGetFlagString(cmd, "name")
		uuid := util.MustGetFlagString(cmd, "uuid")
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.SupportBundleOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}
		}

		err = util.ConfirmCommand(
			fmt.Sprintf(
				"Are you sure you want to delete support bundle: %s from universe %s",
				uuid,
				universeName,
			),
			viper.GetBool("force"),
		)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.SupportBundleOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rDelete, response, err := authAPI.DeleteSupportBundle(universeUUID, uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "Delete")
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The support bundle %s from universe %s (%s) has been deleted",
				formatter.Colorize(uuid, formatter.GreenColor), universeName, universeUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting support bundle %s from universe %s (%s)\n",
						formatter.Colorize(uuid, formatter.GreenColor), universeName, universeUUID),
					formatter.RedColor))
		}

	},
}

func init() {
	deleteSupportBundleUniverseCmd.Flags().SortFlags = false

	deleteSupportBundleUniverseCmd.Flags().StringP("uuid", "u", "",
		"[Required] The UUID of the support bundle to delete.")
	deleteSupportBundleUniverseCmd.MarkFlagRequired("uuid")
}
