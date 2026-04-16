/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundle

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/supportbundle"
)

var listSupportBundleUniverseCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List support bundles for a YugabyteDB Anywhere universe",
	Long:    "List support bundles for a YugabyteDB Anywhere universe",
	Example: `yba universe support-bundle list --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		_ = util.MustGetFlagString(cmd, "name")
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
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.SupportBundleOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		r, response, err := authAPI.ListSupportBundle(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe: Support Bundle", "List")
		}

		supportbundle.Universe = universe

		supportBundleCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  supportbundle.NewSupportBundleFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Infof(
					"No support bundles found for universe %s (%s)\n",
					universeName,
					universeUUID,
				)
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		supportbundle.Write(supportBundleCtx, r)

	},
}

func init() {
	listSupportBundleUniverseCmd.Flags().SortFlags = false

}
