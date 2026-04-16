/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"fmt"
	"net/http"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/user"
)

// createUserCmd represents the universe user command
var createUserCmd = &cobra.Command{
	Use:     "create",
	Short:   "Create a YugabyteDB Anywhere user",
	Long:    "Create an user in YugabyteDB Anywhere",
	Example: `yba user create --email <email> --password <password> --role <role>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		emailFlag, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(emailFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user email found to create\n", formatter.RedColor))
		}

		passwordFlag, err := cmd.Flags().GetString("password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(passwordFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No user password found to create\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		email, err := cmd.Flags().GetString("email")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		password, err := cmd.Flags().GetString("password")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		timezone, err := cmd.Flags().GetString("timezone")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		role, err := cmd.Flags().GetString("role")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		roleResourceDefinitionString, err := cmd.Flags().GetStringArray("role-resource-definition")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rbacAllow, err := rbacutil.RBACRuntimeConfigurationCheck(
			authAPI,
			"User", "Create")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if rbacAllow && !util.IsEmptyString(role) {
			logrus.Fatalln(
				formatter.Colorize(
					"Role cannot be added when yb.rbac.use_new_authz is "+
						"true since this is a deprecated workflow. "+
						"Please use role-resource-definition\n",
					formatter.RedColor))
		} else if rbacAllow && len(roleResourceDefinitionString) == 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"role-resource-definition cannot be empty when yb.rbac.use_new_authz is "+
						"true.\n",
					formatter.RedColor))
		}

		if len(roleResourceDefinitionString) == 0 && util.IsEmptyString(role) {
			logrus.Fatalln(
				formatter.Colorize(
					"Either role or role-resource-definition must be specified.\n",
					formatter.RedColor))
		}

		resourceRoleDefinition := rbacutil.BuildResourceRoleDefinition(
			authAPI, roleResourceDefinitionString)

		req := ybaclient.UserRegistrationData{
			Email:                   email,
			Password:                util.GetStringPointer(password),
			ConfirmPassword:         util.GetStringPointer(password),
			Timezone:                util.GetStringPointer(timezone),
			Role:                    util.GetStringPointer(role),
			RoleResourceDefinitions: resourceRoleDefinition,
		}

		rCreate, response, err := authAPI.CreateUser().User(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "User", "Create")
		}

		createdUser := util.CheckAndDereference(
			rCreate,
			"An error occurred while creating user",
		)

		r := make([]ybaclient.UserWithFeatures, 0)

		r = append(r, createdUser)

		fetchRoleBindingsForListing(createdUser.GetUuid(), authAPI, "Create")

		usersCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  user.NewUserFormat(viper.GetString("output")),
		}
		user.Write(usersCtx, r)

	},
}

func init() {
	createUserCmd.Flags().SortFlags = false
	createUserCmd.Flags().String("email", "e",
		"[Required] The email of the user to be created.")
	createUserCmd.MarkFlagRequired("email")

	createUserCmd.Flags().String("password", "",
		fmt.Sprintf(
			"[Required] Password for the user. Password must contain at "+
				"least 8 characters and at least 1 digit , 1 capital , 1 lowercase"+
				" and 1 of the !@#$^&* (special) characters. Use single quotes ('') to provide "+
				"values with special characters."))
	createUserCmd.MarkFlagRequired("password")
	createUserCmd.Flags().String("timezone", "", "[Optional] The timezone of the user.")
	createUserCmd.Flags().String("role", "",
		fmt.Sprintf(
			"[Optional] The role of the user. "+
				formatter.Colorize(
					"Provide either role or role-resource-definition.",
					formatter.GreenColor)+
				" Allowed values (case sensitive): "+
				"ConnectOnly, ReadOnly, BackupAdmin, Admin, SuperAdmin. %s"+
				" Use role-resource-definition otherwise.",
			formatter.Colorize(
				"Use only when \"yb.rbac.use_new_authz\" runtime configuration is set to false.",
				formatter.GreenColor),
		))
	createUserCmd.Flags().StringArray("role-resource-definition", []string{},
		"[Optional] Role resource bindings for the user. "+
			formatter.Colorize(
				"Provide either role or role-resource-definition.",
				formatter.GreenColor)+
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
