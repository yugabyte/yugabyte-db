/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rolebinding

import (
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/rolebinding"
)

// deleteRoleBindingCmd represents the provider command
var deleteRoleBindingCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere user role binding",
	Long:    "Delete a role binding from a YugabyteDB Anywhere user",
	Example: `yba rbac role-binding delete --uuid <role-binding-uuid> --email <email>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(email) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No user email found to delete role binding\n",
					formatter.RedColor,
				),
			)
		}
		roleBindingUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(roleBindingUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No role binding uuid found to delete\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role Binding", "Delete")

		userListRequest := authAPI.ListUsers()
		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rUsersResponse, response, err := userListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role Binding", "Add - List Users")
		}

		rUsers := make([]ybaclient.UserWithFeatures, 0)
		for _, user := range rUsersResponse {
			if strings.EqualFold(user.GetEmail(), email) {
				rUsers = append(rUsers, user)
			}
		}

		if len(rUsers) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					"No user with email : "+email+" found\n",
					formatter.RedColor))
		}

		userUUID := rUsers[0].GetUuid()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rList, err := authAPI.ListRoleBindingRest(
			userUUID, "RBAC: Role Bindings",
			"Delete - List Role Bindings")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		roleResourceDefintionList := make([]ybaclient.RoleResourceDefinition, 0)

		for _, rb := range rList {
			for _, r := range rb {
				if strings.Compare(r.GetUuid(), uuid) != 0 {
					role := r.GetRole()
					roleResourceDefintion := ybaclient.RoleResourceDefinition{
						RoleUUID: role.GetRoleUUID(),
					}
					if strings.Compare(role.GetRoleType(), util.CustomRoleType) == 0 {
						roleResourceDefintion.SetResourceGroup(r.GetResourceGroup())
					}
					roleResourceDefintionList = append(
						roleResourceDefintionList,
						roleResourceDefintion,
					)
				} else {
					logrus.Debug("Removing role binding with UUID: ", uuid)
				}
			}
		}

		if len(roleResourceDefintionList) == len(rList[userUUID]) {
			logrus.Fatalf(
				formatter.Colorize(
					"No role bindings with UUID : "+uuid+" found\n",
					formatter.RedColor))
		}

		req := ybaclient.RoleBindingFormData{
			RoleResourceDefinitions: roleResourceDefintionList,
		}

		rDelete, response, err := authAPI.SetRoleBinding(userUUID).
			RoleBindingFormData(req).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role Binding", "Delete")
		}
		roleBindingsCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  rolebinding.NewRoleBindingFormat(viper.GetString("output")),
		}
		rolebinding.Write(roleBindingsCtx, rDelete)

	},
}

func init() {
	deleteRoleBindingCmd.Flags().SortFlags = false

	deleteRoleBindingCmd.Flags().StringP("uuid", "u", "",
		"[Required] UUID of the role binding to be deleted.")
	deleteRoleBindingCmd.MarkFlagRequired("uuid")
	deleteRoleBindingCmd.Flags().StringP("email", "e", "",
		"[Required] Email of the user whose role binding you want to delete.")
	deleteRoleBindingCmd.MarkFlagRequired("email")
}
