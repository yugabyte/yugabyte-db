/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteMetadataCmd = &cobra.Command{
	Use:   "delete-metadata",
	Short: "Delete metadata of a universe from a YugabyteDB Anywhere",
	Long: "Delete metadata of a universe from a YugabyteDB Anywhere. " +
		"Metadata shared by other universes is not deleted.",
	Example: `yba universe delete-metadata --name <universe-name>`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		allowed, version, err := authAPI.NewAttachDetachYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !allowed {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"YugabyteDB Anywhere version %s does not support attach/detach universe commands."+
							" Upgrade to stable version %s.\n",
						version,
						util.YBAAllowNewAttachDetachMinStableVersion,
					),
					formatter.RedColor,
				),
			)
		}

		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeName)

		rList, responseList, errList := universeListRequest.Execute()
		if errList != nil {
			util.FatalHTTPError(
				responseList,
				errList,
				"Universe",
				"Delete metadata - List Universes",
			)
		}

		if len(rList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		var universeUUID string
		if len(rList) > 0 {
			universeUUID = rList[0].GetUniverseUUID()
		}

		deleteMetadataRequest := authAPI.DeleteAttachDetachMetadata(universeUUID)
		resp, errDelete := deleteMetadataRequest.Execute()
		if errDelete != nil {
			util.FatalHTTPError(resp, errDelete, "Universe", "Delete metadata")
		}

		logrus.Infof("The metadata for universe %s (%s) has been deleted\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
	},
}

func init() {
	deleteMetadataCmd.Flags().SortFlags = false

	deleteMetadataCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the universe whose metadata is to be deleted, "+
			"at the source YugabyteDB Anywhere.")
	deleteMetadataCmd.MarkFlagRequired("name")
}
