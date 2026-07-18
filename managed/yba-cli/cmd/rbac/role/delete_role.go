/*
 * Copyright (c) YugabyteDB, Inc.
 */

package role

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteRoleCmd represents the role command
var deleteRoleCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere role",
	Long:    "Delete a role in YugabyteDB Anywhere",
	Example: `yba role delete --name <role-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		roleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(roleName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No role name found to delete\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "role", roleName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role", "Delete")

		roleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		roleListRequest := authAPI.ListRoles()

		rList, response, err := roleListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Role", "Delete - List Roles")
		}

		r := make([]ybaclient.Role, 0)

		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(roleName) {
			for _, p := range rList {
				if strings.Contains(strings.ToLower(p.GetName()), strings.ToLower(roleName)) {
					r = append(r, p)
				}
			}
		} else {
			r = rList
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No roles with name: %s found\n", roleName),
					formatter.RedColor,
				))
		}

		var roleUUID string
		if len(r) > 0 {
			roleUUID = r[0].GetRoleUUID()
		}

		deleteRoleRequest := authAPI.DeleteRole(roleUUID)

		rDelete, response, err := deleteRoleRequest.Execute()
		if err != nil {

			util.FatalHTTPError(response, err, "RBAC: Role", "Delete")
		}

		if rDelete.GetSuccess() {
			msg := fmt.Sprintf("The configuration %s (%s) has been deleted",
				formatter.Colorize(roleName, formatter.GreenColor), roleUUID)

			logrus.Infoln(msg + "\n")
		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while removing configuration %s (%s)\n",
						roleName, roleUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteRoleCmd.Flags().SortFlags = false
	deleteRoleCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the role to be deleted.")
	deleteRoleCmd.MarkFlagRequired("name")
	deleteRoleCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
