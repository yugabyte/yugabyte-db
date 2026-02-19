package restore

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/restore"
)

// listRestoreCmd represents the list restore command
var listRestoreCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere restores",
	Long:    "List restores in YugabyteDB Anywhere",
	Example: `yba backup restore list --universe-uuids <universe-uuid-1>,<universe-uuid-2> \
	--universe-names <universe-name-1>,<universe-name-2>`,
	Run: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeUUIDs, err := cmd.Flags().GetString("universe-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeNames, err := cmd.Flags().GetString("universe-names")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		restoreCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  restore.NewRestoreFormat(viper.GetString("output")),
		}

		var limit int32 = 10
		var offset int32 = 0
		restoreAPIDirection := util.DescSortDirection
		restoreAPISort := "createTime"

		universeNamesList := []string{}
		universeUUIDsList := []string{}
		if !util.IsEmptyString(universeNames) {
			universeNamesList = strings.Split(universeNames, ",")
		}

		if !util.IsEmptyString(universeUUIDs) {
			universeUUIDsList = strings.Split(universeUUIDs, ",")
		}
		restoreAPIFilter := ybaclient.RestoreApiFilter{
			UniverseNameList: universeNamesList,
			UniverseUUIDList: universeUUIDsList,
		}

		restoreAPIQuery := ybaclient.RestorePagedApiQuery{
			Filter:    restoreAPIFilter,
			Direction: restoreAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    restoreAPISort,
		}

		restoreListRequest := authAPI.ListRestores().PageRestoresRequest(restoreAPIQuery)
		restores := make([]ybaclient.RestoreResp, 0)
		force := viper.GetBool("force")
		for {
			// Execute backup list request
			r, response, err := restoreListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Restore", "List")
			}

			// Check if backups found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Info("No restores found\n")
				} else {
					logrus.Info("[]\n")
				}
				return
			}

			// Write restore entities
			if force {
				restores = append(restores, r.GetEntities()...)
			} else {
				restore.Write(restoreCtx, r.GetEntities())
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				if util.IsOutputType(formatter.TableFormatKey) && !force {
					logrus.Info("No more restores present\n")
				}
				break
			}

			err = util.ConfirmCommand(
				"List more entries",
				force)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			restoreAPIQuery.Offset = offset
			restoreListRequest = authAPI.ListRestores().PageRestoresRequest(restoreAPIQuery)
		}
		if force {
			restore.Write(restoreCtx, restores)
		}

	},
}

func init() {
	listRestoreCmd.Flags().SortFlags = false
	listRestoreCmd.Flags().String("universe-uuids", "",
		"[Optional] Comma separated list of universe uuids")
	listRestoreCmd.Flags().String("universe-names", "",
		"[Optional] Comma separated list of universe names")
	listRestoreCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
