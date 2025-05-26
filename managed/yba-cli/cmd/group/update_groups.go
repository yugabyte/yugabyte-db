/*
 * Copyright (c) YugaByte, Inc.
 */

package group

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// UpdateGroupCmd set of commands are used to manage groups in YugabyteDB Anywhere
var UpdateGroupCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"modify", "edit"},
	Short:   "Update a group",
	Long:    "Update group mapping for LDAP/OIDC",
	Example: "yba group update -n <group-name> --role-resource-definition <role-resource-definition>",
	Run: func(cmd *cobra.Command, args []string) {
		groupName := util.MustGetFlagString(cmd, "name")
		roleResourceDefinitions := util.MaybeGetFlagStringArray(cmd, "role-resource-definition")
		roleResuorceDefinitionsToAdd := util.MaybeGetFlagStringArray(
			cmd,
			"add-role-resource-definition",
		)
		rolesToRemove := util.MaybeGetFlagStringSlice(cmd, "remove-roles")
		if len(roleResourceDefinitions) == 0 && len(roleResuorceDefinitionsToAdd) == 0 &&
			len(rolesToRemove) == 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"At least one of --role-resource-definition, --add-role-resource-definition or --remove-roles must be provided.",
					formatter.RedColor,
				),
			)
		}
		if len(roleResourceDefinitions) > 0 &&
			(len(roleResuorceDefinitionsToAdd) > 0 || len(rolesToRemove) > 0) {
			logrus.Warnln(
				formatter.Colorize(
					"Ignoring --add-role-resource-definition and --remove-roles flags since --role-resource-definition is set.\n",
					formatter.YellowColor,
				),
			)

		}
		UpdateGroupUtil(UpdateParams{
			groupName:                 groupName,
			roleResourceDefinition:    roleResourceDefinitions,
			addRoleResourceDefinition: roleResuorceDefinitionsToAdd,
			removeRoles:               rolesToRemove,
		})

	},
}

func init() {
	UpdateGroupCmd.Flags().SortFlags = false
	UpdateGroupCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the group to update.")
	UpdateGroupCmd.MarkFlagRequired("name")

	UpdateGroupCmd.Flags().StringArray("role-resource-definition", []string{},
		`[Optional] Set the exact list of roles-definitions for the group, replacing any existing roles.
		 Provide the following double colon (::) separated fields as key-value pairs:
		 "role-uuid=<role-uuid>::allow-all=<true/false>::resource-type=<resource-type>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>".
		 `+formatter.Colorize("Role UUID is a required value. ", formatter.GreenColor)+
			`
		 Allowed values for resource type are universe, role, user, other.
		 If resource UUID list is empty, allow all is set to true.
		 If the role given is a system role, resource type, allow all and
		 resource UUID must be empty.
		 Add multiple resource types for each role UUID
		 using separate --role-resource-definition flags.
		 Each binding needs to be added using a separate --role-resource-definition flag.
		 Example: --role-resource-definition
		 role-uuid=<role-uuid1>::resource-type=<resource-type1>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
		 --role-resource-definition
		 role-uuid=<role-uuid2>::resource-type=<resource-type1>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
		 --role-resource-definition
		 role-uuid=<role-uuid2>::resource-type=<resource-type2>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>.
		 Setting this will ignore add-role-resource-definition and remove-roles flags.`)

	UpdateGroupCmd.Flags().StringArray("add-role-resource-definition", []string{},
		`[Optional] Add one or more role-resource-definitions to the group.
		 Input format is same as --role-resource-definition.
		 Example: --add-role-resource-definition role-uuid=<role-uuid1>::resource-type=<resource-type1>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3>
		 --add-role-resource-definition role-uuid=<role-uuid2>::resource-type=<resource-type1>::
		 resource-uuid=<resource-uuid1>,<resource-uuid2>,<resource-uuid3> `)

	UpdateGroupCmd.Flags().StringSlice("remove-roles", []string{},
		"[Optional] Remove one or more roles from the group. \n"+
			"Example: --remove-roles role1_uuid,role2_uuid or --remove-roles role1_uuid --remove-roles role2_uuid.")
}
