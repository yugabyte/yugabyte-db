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

// addRoleBindingCmd represents the provider command
var addRoleBindingCmd = &cobra.Command{
	Use:     "add",
	Aliases: []string{"append"},
	Short:   "Add a YugabyteDB Anywhere user role binding",
	Long:    "Add a role binding to a YugabyteDB Anywhere user",
	Example: `yba rbac role-binding add --email <email> \
	--role-resource-definition role-uuid=<role-uuid1>::resource-type=<resource-type1>:: \
	resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(email) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user email found to add role binding\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role Binding", "Add")

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

		rList, err := authAPI.ListRoleBindingRest(
			userUUID,
			"RBAC: Role Bindings",
			"Add - List Role Bindings")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		roleResourceDefintionList := make([]ybaclient.RoleResourceDefinition, 0)
		for _, rb := range rList {
			for _, r := range rb {
				role := r.GetRole()
				roleResourceDefintion := ybaclient.RoleResourceDefinition{
					RoleUUID: role.GetRoleUUID(),
				}
				if strings.Compare(role.GetRoleType(), util.CustomRoleType) == 0 {
					roleResourceDefintion.SetResourceGroup(r.GetResourceGroup())
				}
				roleResourceDefintionList = append(roleResourceDefintionList, roleResourceDefintion)
			}
		}

		roleResourceDefinitionString, err := cmd.Flags().GetStringArray("role-resource-definition")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		resourceRoleDefinition := rbacutil.BuildResourceRoleDefinition(
			authAPI, roleResourceDefinitionString)

		roleResourceDefintionList = append(roleResourceDefintionList, resourceRoleDefinition...)

		req := ybaclient.RoleBindingFormData{
			RoleResourceDefinitions: roleResourceDefintionList,
		}

		rAdd, response, err := authAPI.SetRoleBinding(userUUID).RoleBindingFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role Binding", "Add")
		}
		roleBindingsCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  rolebinding.NewRoleBindingFormat(viper.GetString("output")),
		}
		rolebinding.Write(roleBindingsCtx, rAdd)

	},
}

func init() {
	addRoleBindingCmd.Flags().SortFlags = false

	addRoleBindingCmd.Flags().StringP("email", "e", "",
		"[Required] Email of the user to add role binding")
	addRoleBindingCmd.MarkFlagRequired("email")

	addRoleBindingCmd.Flags().StringArray("role-resource-definition", []string{},
		"[Optional] Role resource bindings to be added. "+
			" Provide the following double colon (::) separated fields as key-value pairs:"+
			"\"role-uuid=<role-uuid>::allow-all=<true/false>::resource-type=<resource-type>"+
			"::resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>\". "+
			formatter.Colorize("Role UUID is a required value. ",
				formatter.GreenColor)+
			"Allowed values for resource type are universe, role, user, other. "+
			"If resource UUID list is empty, default for allow all is true. "+
			"If the role given is a system role, resource type, allow all and "+
			"resource UUID must be empty."+
			" Add multiple resource types for each role UUID "+
			" using separate --role-resource-definition flags. "+
			"Each binding needs to be added using a separate --role-resource-definition flag. "+
			"Example: --role-resource-definition "+
			"role-uuid=<role-uuid1>::resource-type=<resource-type1>::"+
			"resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> "+
			"--role-resource-definition "+
			"role-uuid=<role-uuid2>::resource-type=<resource-type1>::"+
			"resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> "+
			"--role-resource-definition "+
			"role-uuid=<role-uuid2>::resource-type=<resource-type2>::"+
			"resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>")

}
