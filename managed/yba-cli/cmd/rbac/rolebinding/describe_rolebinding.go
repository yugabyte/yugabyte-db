/*
 * Copyright (c) YugaByte, Inc.
 */

package rolebinding

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/rolebinding"
)

var describeRoleBindingCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere RBAC role binding",
	Long:    "Describe a RBAC role binding in YugabyteDB Anywhere",
	Example: `yba rbac role-binding describe --uuid <role-binding-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		roleBindingUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(roleBindingUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No role binding uuid found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role Binding", "Describe")

		rList, err := authAPI.ListRoleBindingRest("", "Describe")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r := make([]ybaclient.RoleBinding, 0)

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		for _, rb := range rList {
			r = append(r, rb...)
		}

		for _, rb := range r {
			if strings.Compare(rb.GetUuid(), uuid) == 0 {
				r = []ybaclient.RoleBinding{rb}
				break
			}
		}

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullRoleBindingContext := *rolebinding.NewFullRoleBindingContext()
			fullRoleBindingContext.Output = os.Stdout
			fullRoleBindingContext.Format = rolebinding.NewFullRoleBindingFormat(
				viper.GetString("output"),
			)
			fullRoleBindingContext.SetFullRoleBinding(r[0])
			fullRoleBindingContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No role bindings with uuid: %s found\n", uuid),
					formatter.RedColor,
				))
		}

		roleBindingCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  rolebinding.NewRoleBindingFormat(viper.GetString("output")),
		}
		rolebinding.Write(roleBindingCtx, r)

	},
}

func init() {

	describeRoleBindingCmd.Flags().SortFlags = false

	describeRoleBindingCmd.Flags().StringP("uuid", "u", "",
		"[Required] UUID of the role binding.")
	describeRoleBindingCmd.MarkFlagRequired("uuid")

}
