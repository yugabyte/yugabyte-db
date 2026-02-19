/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permission

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/permissioninfo"
)

var describePermissionCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere RBAC permission",
	Long:    "Describe a RBAC permission in YugabyteDB Anywhere",
	Example: `yba rbac permission describe --name <permission-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		permissionNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(permissionNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No permission name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Permission", "Describe")

		permissionListRequest := authAPI.ListPermissions()

		rList, response, err := permissionListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Permission", "Describe")
		}

		r := make([]ybaclient.PermissionInfo, 0)

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(name) {
			for _, p := range rList {
				if strings.Contains(strings.ToLower(p.GetName()), strings.ToLower(name)) {
					r = append(r, p)
				}
			}
		} else {
			r = rList
		}

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullPermissionContext := *permissioninfo.NewFullPermissionInfoContext()
			fullPermissionContext.Output = os.Stdout
			fullPermissionContext.Format = permissioninfo.NewFullPermissionInfoFormat(
				viper.GetString("output"),
			)
			fullPermissionContext.SetFullPermissionInfo(r[0])
			fullPermissionContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No permissions with name: %s found\n", name),
					formatter.RedColor,
				))
		}

		permissionCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  permissioninfo.NewPermissionInfoFormat(viper.GetString("output")),
		}
		permissioninfo.Write(permissionCtx, r)

	},
}

func init() {

	describePermissionCmd.Flags().SortFlags = false

	describePermissionCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the permission. Quote name if it contains space.")
	describePermissionCmd.MarkFlagRequired("name")

}
