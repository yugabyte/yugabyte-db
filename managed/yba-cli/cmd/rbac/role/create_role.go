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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/rbac/role"
)

// createRoleCmd represents the ear command
var createRoleCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create YugabyteDB Anywhere RBAC roles",
	Long:    "Create YugabyteDB Anywhere RBAC roles",
	Example: `yba rbac role create --name <role-name> \
	--permission resource-type=other::action=read \
	--description <description>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			cmd.Help()
			logrus.Fatalln(formatter.Colorize("No role name found to create\n", formatter.RedColor))
		}
		permissionList, err := cmd.Flags().GetStringArray("permission")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(permissionList) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No permissions found to create\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role", "Create")

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		permissionList, err := cmd.Flags().GetStringArray("permission")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		permissions := buildPermissions(permissionList)

		req := ybaclient.RoleFormData{
			Name:           name,
			Description:    description,
			PermissionList: permissions,
		}

		r, response, err := authAPI.CreateRole().RoleFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role", "Create")
		}

		createdRole := util.CheckAndDereference(r, "Role creation failed")

		roles := []ybaclient.Role{createdRole}
		rolesCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  role.NewRoleFormat(viper.GetString("output")),
		}
		role.Write(rolesCtx, roles)

	},
}

func init() {
	createRoleCmd.Flags().SortFlags = false

	createRoleCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the role. Quote name if it contains space.")
	createRoleCmd.MarkFlagRequired("name")
	createRoleCmd.Flags().String("description", "",
		"[Optional] Description of the role. Quote description if it contains space.")
	createRoleCmd.Flags().StringArray("permission", []string{},
		"[Required] Permissions associated with the role. Minimum number of required "+
			"permissions = 1. Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"resource-type=<resource-type>::action=<action>\". "+
			formatter.Colorize("Both are requires key-values. ",
				formatter.GreenColor)+
			"Allowed resource types are universe, role, user, other. "+
			"Allowed actions are create, read, update, delete, pause_resume, "+
			"backup_restore, update_role_bindings, update_profile, super_admin_actions, "+
			"xcluster."+
			" Each permission needs to be added using a separate --permission flag. "+
			"Example: --permission resource-type=other::action=delete "+
			"--permission resource-type=universe::action=write")
}

func buildPermissions(permissionStrings []string) (res []ybaclient.Permission) {
	if len(permissionStrings) == 0 {
		logrus.Fatalln(
			formatter.Colorize("Atleast one permission is required per role.\n",
				formatter.RedColor))
	}
	for _, permissionString := range permissionStrings {
		permission := buildPermissionMapFromString(permissionString)
		r := ybaclient.Permission{
			ResourceType: util.GetStringPointer(permission["resource-type"]),
			Action:       util.GetStringPointer(permission["action"]),
		}

		res = append(res, r)
	}
	return res
}

func buildPermissionMapFromString(permissionString string) (res map[string]string) {
	permission := map[string]string{}
	for _, permissionInfo := range strings.Split(permissionString, util.Separator) {
		kvp := strings.Split(permissionInfo, "=")
		if len(kvp) != 2 {
			logrus.Fatalln(
				formatter.Colorize("Incorrect format in permission description.\n",
					formatter.RedColor))
		}
		key := kvp[0]
		val := kvp[1]
		switch key {
		case "resource-type":
			if !util.IsEmptyString(val) {
				permission["resource-type"] = strings.ToUpper(val)
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		case "action":
			if !util.IsEmptyString(val) {
				permission["action"] = strings.ToUpper(val)
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		}
	}
	if _, ok := permission["action"]; !ok {
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("Action not specified in permission.\n"),
				formatter.RedColor))
	}

	if _, ok := permission["resource-type"]; !ok {
		logrus.Fatalln(
			formatter.Colorize(
				fmt.Sprintf("Resource type not specified in permission.\n"),
				formatter.RedColor))
	}
	return permission
}
