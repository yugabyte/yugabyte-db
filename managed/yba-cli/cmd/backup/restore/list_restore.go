package restore

import (
	"fmt"
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
	Use:   "list",
	Short: "List YugabyteDB Anywhere restores",
	Long:  "List restores in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()

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
		restoreAPIDirection := "DESC"
		restoreAPISort := "createTime"

		universeNamesList := []string{}
		universeUUIDsList := []string{}
		if (len(strings.TrimSpace(universeNames))) > 0 {
			universeNamesList = strings.Split(universeNames, ",")
		}

		if (len(strings.TrimSpace(universeUUIDs))) > 0 {
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

		for {
			// Execute backup list request
			r, response, err := restoreListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Restore", "List")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			// Check if backups found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Infoln("No restores found\n")
				} else {
					logrus.Infoln("[]\n")
				}
				return
			}

			// Write backup entities
			restore.Write(restoreCtx, r.GetEntities())

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				logrus.Infoln("No more restores present\n")
				break
			}

			// Prompt user for more entries
			if !promptForMoreEntries() {
				break
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			restoreAPIQuery.Offset = offset
			restoreListRequest = authAPI.ListRestores().PageRestoresRequest(restoreAPIQuery)
		}

	},
}

// Function to prompt user for more entries
func promptForMoreEntries() bool {
	var input string
	fmt.Print("More entries? (yes/no): ")
	fmt.Scanln(&input)
	return strings.ToLower(input) == "yes"
}

func init() {
	listRestoreCmd.Flags().SortFlags = false
	listRestoreCmd.Flags().String("universe-uuids", "",
		"[Optional] Comma separated list of universe uuids")
	listRestoreCmd.Flags().String("universe-names", "",
		"[Optional] Comma separated list of universe names")
}
