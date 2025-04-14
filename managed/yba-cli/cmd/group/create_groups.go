package group

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// CreateGroupCmd set of commands are used to manage groups in YugabyteDB Anywhere
var CreateGroupCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a new group",
	Long:    "Create a new group mapping for LDAP/OIDC",
	Example: "yba group create -n <group-name> -c <auth-code> -role-resource-definition <role-resource-definition>",
	Run: func(cmd *cobra.Command, args []string) {
		groupName := util.MustGetFlagString(cmd, "name")
		authCode := strings.ToUpper(util.MustGetFlagString(cmd, "auth-code"))
		if authCode != util.LDAPGroupMappingType && authCode != util.OIDCGroupMappingType {
			logrus.Fatalln(
				formatter.Colorize(
					fmt.Sprintf(
						"Authentication code provided %s is not supported. Accepted values - LDAP/OIDC",
						authCode,
					),
					formatter.RedColor,
				),
			)
		}
		roles := util.MustGetStringArray(cmd, "role-resource-definition")
		CreateGroupUtil(cmd, groupName, roles, authCode)
	},
}

func init() {
	CreateGroupCmd.Flags().SortFlags = false
	CreateGroupCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the group mapping. Use group name for OIDC and Group DN for LDAP.")
	CreateGroupCmd.MarkFlagRequired("name")
	CreateGroupCmd.Flags().StringP("auth-code", "c", "",
		"[Required] Authentication code of the group(LDAP/OIDC)")
	CreateGroupCmd.MarkFlagRequired("auth-code")
	CreateGroupCmd.Flags().StringArray("role-resource-definition", []string{},
		"[Required] Role resource bindings to be added. "+
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
	CreateGroupCmd.MarkFlagRequired("role-resource-definition")

}
