/*
 * Copyright (c) YugaByte, Inc.
 */

package rolebinding

import (
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/rolebinding"
)

var listRoleBindingCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere role bindings",
	Long:    "List YugabyteDB Anywhere role bindings",
	Example: `yba rbac role-binding list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role Binding", "List")

		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		userUUID := ""

		if len(strings.TrimSpace(email)) != 0 {
			rUsers, response, err := authAPI.ListUsers().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response,
					err,
					"RBAC: Role Binding",
					"List - List Users")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			r := make([]ybaclient.UserWithFeatures, 0)
			for _, user := range rUsers {
				if strings.Compare(user.GetEmail(), email) == 0 {
					r = append(r, user)
				}
			}
			if len(r) > 0 {
				userUUID = r[0].GetUuid()
			}
		}
		rList, err := authAPI.ListRoleBindingRest(userUUID, "List")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r := make([]ybaclient.RoleBinding, 0)
		for _, rb := range rList {
			r = append(r, rb...)
		}

		roleBindingCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  rolebinding.NewRoleBindingFormat(viper.GetString("output")),
		}

		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No role bindings found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		rolebinding.Write(roleBindingCtx, r)

	},
}

func init() {
	listRoleBindingCmd.Flags().SortFlags = false

	listRoleBindingCmd.Flags().StringP("email", "e", "",
		"[Optional] Email of the user whose role binding you want to list.")
}
