/*
 * Copyright (c) YugabyteDB, Inc.
 */

package filecollection

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	fcformatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/filecollection"
)

var deleteFileCollectionCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"rm", "remove"},
	Short:   "Delete collected files from database nodes and/or YBA",
	Long: "Delete tar archives created by the create file collection command. " +
		"By default, deletes files from DB nodes. Use --delete-from-yba to also " +
		"remove downloaded files from YBA local storage.",
	Example: `yba universe file-collection delete --name <universe-name> --uuid <collection-uuid>

yba universe file-collection delete --name <universe-name> --uuid <collection-uuid> \
  --delete-from-yba --force`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName := util.MustGetFlagString(cmd, "name")
		uuid := util.MustGetFlagString(cmd, "uuid")
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, "FileCollectionOperation")
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}
		}

		err = util.ConfirmCommand(
			fmt.Sprintf(
				"Are you sure you want to delete file collection: %s from universe %s",
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
		authAPI, universe, err := universeutil.Validations(cmd, util.FileCollectionOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		collectionUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteFromDbNodes, err := cmd.Flags().GetBool("delete-from-db-nodes")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteFromYba, err := cmd.Flags().GetBool("delete-from-yba")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteRequest := authAPI.DeleteFileCollection(universeUUID, collectionUUID).
			DeleteFromDbNodes(deleteFromDbNodes).
			DeleteFromYba(deleteFromYba)

		resp, httpResponse, err := deleteRequest.Execute()
		if err != nil {
			util.FatalHTTPError(
				httpResponse, err, "Universe: File Collection", "Delete",
			)
		}

		logrus.Info(
			fmt.Sprintf(
				"File collection %s from universe %s (%s) has been deleted\n",
				formatter.Colorize(collectionUUID, formatter.GreenColor),
				universeName,
				universeUUID,
			),
		)

		ctx := formatter.Context{
			Command: "delete",
			Output:  os.Stdout,
			Format:  fcformatter.NewCleanupFormat(viper.GetString("output")),
		}
		fcformatter.WriteCleanup(ctx, resp)
	},
}

func init() {
	deleteFileCollectionCmd.Flags().SortFlags = false

	deleteFileCollectionCmd.Flags().StringP("uuid", "u", "",
		"[Required] The collection UUID to delete.")
	deleteFileCollectionCmd.MarkFlagRequired("uuid")
	deleteFileCollectionCmd.Flags().Bool("delete-from-db-nodes", true,
		"[Optional] Whether to delete tar files from database nodes.")
	deleteFileCollectionCmd.Flags().Bool("delete-from-yba", false,
		"[Optional] Whether to also delete downloaded files from YBA local storage. "+
			"(default false)")
}
