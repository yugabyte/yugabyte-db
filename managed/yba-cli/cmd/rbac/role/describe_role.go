/*
 * Copyright (c) YugabyteDB, Inc.
 */

package role

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/role"
)

var describeRoleCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere RBAC role",
	Long:    "Describe a RBAC role in YugabyteDB Anywhere",
	Example: `yba rbac role describe --name <role-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		roleNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(roleNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No role name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role", "Describe")

		roleListRequest := authAPI.ListRoles()

		rList, response, err := roleListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role", "Describe")
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

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullRoleContext := *role.NewFullRoleContext()
			fullRoleContext.Output = os.Stdout
			fullRoleContext.Format = role.NewFullRoleFormat(viper.GetString("output"))
			fullRoleContext.SetFullRole(r[0])
			fullRoleContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No roles with name: %s found\n", name),
					formatter.RedColor,
				))
		}

		roleCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  role.NewRoleFormat(viper.GetString("output")),
		}
		role.Write(roleCtx, r)

	},
}

func init() {

	describeRoleCmd.Flags().SortFlags = false

	describeRoleCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the role. Quote name if it contains space.")
	describeRoleCmd.MarkFlagRequired("name")

}
