/*
 * Copyright (c) YugabyteDB, Inc.
 */

package permission

import (
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/permissioninfo"
)

var listPermissionCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere permissions",
	Long:    "List YugabyteDB Anywhere permissions",
	Example: `yba rbac permission list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Permission", "List")
		permissionListRequest := authAPI.ListPermissions()
		// filter by resourceType and/or by permission code
		resourceType, err := cmd.Flags().GetString("resource-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rList := make([]ybaclient.PermissionInfo, 0)
		var response *http.Response
		if !util.IsEmptyString(resourceType) {
			permissionListRequest = permissionListRequest.ResourceType(
				strings.ToUpper(resourceType),
			)
			rList, response, err = permissionListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "RBAC: Permission", "List")
			}
		} else {
			resourceTypes := []string{
				util.RoleResourceType,
				util.UserResourceType,
				util.UniverseResourceType,
				util.OtherResourceType,
			}
			for _, c := range resourceTypes {
				permissionListRequest = permissionListRequest.ResourceType(c)
				rCode, response, err := permissionListRequest.Execute()
				if err != nil {
					util.FatalHTTPError(response, err, "RBAC: Permission", "List")
				}
				rList = append(rList, rCode...)
			}
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

		permissionCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  permissioninfo.NewPermissionInfoFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No permissions found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		permissioninfo.Write(permissionCtx, r)

	},
}

func init() {
	listPermissionCmd.Flags().SortFlags = false

	listPermissionCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the permission. Quote name if it contains space.")
	listPermissionCmd.Flags().String("resource-type", "",
		"[Optional] Resource type of the permission. Allowed values: "+
			"universe, role, user, other. If not specified, all resource types are returned.")
}
