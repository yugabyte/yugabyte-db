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

// updateRoleCmd represents the provider command
var updateRoleCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere role",
	Long:    "Update a role in YugabyteDB Anywhere",
	Example: `yba rbac role update --name <role-name> \
	--add-permission resource-type=other::action=create`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			cmd.Help()
			logrus.Fatalln(formatter.Colorize("No role name found to create\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		rbacutil.CheckRBACEnablementOnYBA(authAPI, "RBAC: Role", "Update")

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rList, response, err := authAPI.ListRoles().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role", "Update - List Roles")
		}
		r := make([]ybaclient.Role, 0)
		if !util.IsEmptyString(name) {
			for _, p := range rList {
				if strings.Contains(strings.ToLower(p.GetName()), strings.ToLower(name)) {
					r = append(r, p)
				}
			}
		} else {
			r = rList
		}
		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No roles with name: %s found to update\n", name),
					formatter.RedColor,
				))
		}
		updateRole := r[0]

		rolePermissionDetails := updateRole.GetPermissionDetails()
		rolePermissions := rolePermissionDetails.GetPermissionList()

		addPermission, err := cmd.Flags().GetStringArray("add-permission")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		removePermission, err := cmd.Flags().GetStringArray("remove-permission")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rolePermissions = removePermissions(removePermission, rolePermissions)
		rolePermissions = addPermissions(addPermission, rolePermissions)

		req := ybaclient.RoleFormData{
			Name:           updateRole.GetName(),
			Description:    updateRole.GetDescription(),
			PermissionList: rolePermissions,
		}

		rUpdate, response, err := authAPI.EditRole(updateRole.GetRoleUUID()).
			RoleFormData(req).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "RBAC: Role", "Update")
		}

		updatedRole := util.CheckAndDereference(
			rUpdate,
			fmt.Sprintf("An error occurred while updating role with name: %s", name),
		)

		roles := []ybaclient.Role{updatedRole}
		rolesCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  role.NewRoleFormat(viper.GetString("output")),
		}
		role.Write(rolesCtx, roles)

	},
}

func init() {
	updateRoleCmd.Flags().SortFlags = false

	updateRoleCmd.Flags().StringP("name", "n", "", "[Required] Role name to be updated.")
	updateRoleCmd.MarkFlagRequired("name")

	updateRoleCmd.Flags().StringArray("add-permission", []string{},
		"[Optional] Add permissions to the role. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"resource-type=<resource-type>::action=<action>\". "+
			formatter.Colorize("Both are requires key-values. ",
				formatter.GreenColor)+
			"Allowed resource types are: universe, role, user, other. "+
			"Allowed actions are: create, read, update, delete, pause_resume, "+
			"backup_restore, update_role_bindings, update_profile, super_admin_actions, "+
			"xcluster."+
			"Quote action if it contains space."+
			" Each permission needs to be added using a separate --add-permission flag.")

	updateRoleCmd.Flags().StringArray("remove-permission", []string{},
		"[Optional] Remove permissions from the role. "+
			"Provide the following double colon (::) separated fields as key-value pairs: "+
			"\"resource-type=<resource-type>::action=<action>\". "+
			formatter.Colorize("Both are requires key-values. ",
				formatter.GreenColor)+
			"Allowed resource types are: universe, role, user, other. "+
			"Allowed actions are: create, read, update, delete, pause_resume, "+
			"backup_restore, update_role_bindings, update_profile, super_admin_actions, "+
			"xcluster."+
			"Quote action if it contains space."+
			" Each permission needs to be removed using a separate --remove-permission flag.")

}

func removePermissions(
	removePermissions []string,
	rolePermissions []ybaclient.Permission) []ybaclient.Permission {
	if len(removePermissions) == 0 {
		return rolePermissions
	}

	for _, permissionString := range removePermissions {
		permission := buildPermissionMapFromString(permissionString)
		for i, r := range rolePermissions {
			if r.GetResourceType() == permission["resource-type"] &&
				r.GetAction() == permission["action"] {
				rolePermissions = append(rolePermissions[:i], rolePermissions[i+1:]...)
			}
		}
	}

	return rolePermissions
}

func addPermissions(
	addPermissions []string,
	rolePermissions []ybaclient.Permission,
) []ybaclient.Permission {
	if len(addPermissions) == 0 {
		return rolePermissions
	}
	for _, permissionString := range addPermissions {
		permission := buildPermissionMapFromString(permissionString)
		r := ybaclient.Permission{
			ResourceType: util.GetStringPointer(permission["resource-type"]),
			Action:       util.GetStringPointer(permission["action"]),
		}
		rolePermissions = append(rolePermissions, r)
	}
	return rolePermissions
}
