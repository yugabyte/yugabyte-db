/*
 * Copyright (c) YugabyteDB, Inc.
 */

package role

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/role"
)

var listRoleCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere roles",
	Long:    "List YugabyteDB Anywhere roles",
	Example: `yba rbac role list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role", "List")

		roleListRequest := authAPI.ListRoles()
		// filter by roleType and/or by role code
		roleType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		rList := make([]ybaclient.Role, 0)
		var response *http.Response
		if !util.IsEmptyString(roleType) {
			roleListRequest = roleListRequest.RoleType(
				strings.ToUpper(roleType),
			)
			rList, response, err = roleListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "RBAC: Role", "List")
			}
		} else {
			roleTypes := []string{
				util.SystemRoleType,
				util.CustomRoleType,
			}
			for _, c := range roleTypes {
				roleListRequest = roleListRequest.RoleType(c)
				rCode, response, err := roleListRequest.Execute()
				if err != nil {
					util.FatalHTTPError(response, err, "RBAC: Role", "List")
				}
				rList = append(rList, rCode...)
			}
		}

		r := make([]ybaclient.Role, 0)

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

		roleCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  role.NewRoleFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No roles found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		role.Write(roleCtx, r)

	},
}

func init() {
	listRoleCmd.Flags().SortFlags = false

	listRoleCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the role. Quote name if it contains space.")
	listRoleCmd.Flags().String("type", "",
		"[Optional] Role type. Allowed values: "+
			"system, custom. If not specified, all role types are returned.")
}
