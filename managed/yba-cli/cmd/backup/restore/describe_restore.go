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

// describeRestoreCmd represents the list restore command
var describeRestoreCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "List YugabyteDB Anywhere restores",
	Long:    "List restores in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()

		restoreUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var limit int32 = 10
		var offset int32 = 0
		restoreAPIDirection := "DESC"
		restoreAPISort := "createTime"

		restoreUUIDList := make([]string, 0)
		if len(strings.TrimSpace(restoreUUID)) > 0 {
			restoreUUIDList = append(restoreUUIDList, restoreUUID)
		}

		restoreAPIFilter := ybaclient.RestoreApiFilter{
			RestoreUUIDList: restoreUUIDList,
		}

		restoreAPIQuery := ybaclient.RestorePagedApiQuery{
			Filter:    restoreAPIFilter,
			Direction: restoreAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    restoreAPISort,
		}

		restoreListRequest := authAPI.ListRestores().PageRestoresRequest(restoreAPIQuery)

		// Execute restore list request
		r, response, err := restoreListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Restore", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r.GetEntities()) > 0 && util.IsOutputType("table") {
			fullRestoreContext := *restore.NewFullRestoreContext()
			fullRestoreContext.Output = os.Stdout
			fullRestoreContext.Format = restore.NewRestoreFormat(viper.GetString("output"))
			fullRestoreContext.SetFullRestore(r.GetEntities()[0])
			fullRestoreContext.Write()
			return
		}

		if len(r.GetEntities()) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No restores with UUID: %s found\n", restoreUUID),
					formatter.RedColor,
				))
		}

		restoreCtx := formatter.Context{
			Output: os.Stdout,
			Format: restore.NewRestoreFormat(viper.GetString("output")),
		}
		restore.Write(restoreCtx, r.GetEntities())

	},
}

func init() {
	describeRestoreCmd.Flags().SortFlags = false
	describeRestoreCmd.Flags().String("uuid", "",
		"[Required] UUID of restore to be described")
	describeRestoreCmd.MarkFlagRequired("uuid")
}
