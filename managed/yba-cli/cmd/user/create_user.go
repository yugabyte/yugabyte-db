/*
 * Copyright (c) YugaByte, Inc.
 */

package user

import (
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
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
	Example: `yba user create `,
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

		// check yb.rbac.use_new_authz flag before the role/ resource binding creation
		scopeUUID := "00000000-0000-0000-0000-000000000000" // global scope
		key := "yb.rbac.use_new_authz"
		rbacAllow, response, err := authAPI.GetConfigurationKey(scopeUUID, key).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response,
				err,
				"User", "Create - Get Runtime Configuration Key")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if strings.Compare(rbacAllow, "true") == 0 && len(strings.TrimSpace(role)) != 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"Role cannot be added when yb.rbac.use_new_authz is "+
						"true since this is a deprecated workflow. "+
						"Please use role-resource-definition\n",
					formatter.RedColor))
		} else if strings.Compare(rbacAllow, "true") == 0 && len(roleResourceDefinitionString) == 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"role-resource-definition cannot be empty when yb.rbac.use_new_authz is "+
						"true.\n",
					formatter.RedColor))
		}

		if len(roleResourceDefinitionString) == 0 && len(strings.TrimSpace(role)) == 0 {
			logrus.Fatalln(
				formatter.Colorize(
					"Either role or role-resource-definition must be specified.\n",
					formatter.RedColor))
		}

		resourceRoleDefinition := buildResourceRoleDefinition(roleResourceDefinitionString)

		req := ybaclient.UserRegistrationData{
			Email:                   email,
			Password:                util.GetStringPointer(password),
			ConfirmPassword:         util.GetStringPointer(password),
			Timezone:                util.GetStringPointer(timezone),
			Role:                    util.GetStringPointer(role),
			RoleResourceDefinitions: &resourceRoleDefinition,
		}

		rCreate, response, err := authAPI.CreateUser().User(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "User", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		r := make([]ybaclient.UserWithFeatures, 0)

		r = append(r, rCreate)

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
					"Provider either role or role-resource-definition.",
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
				"Provider either role or role-resource-definition.",
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

func buildResourceRoleDefinition(
	roleResourceDefinitionStrings []string) []ybaclient.RoleResourceDefinition {
	if len(roleResourceDefinitionStrings) == 0 {
		return nil
	}
	res := make([]ybaclient.RoleResourceDefinition, 0)

	for _, roleResourceDefinitionString := range roleResourceDefinitionStrings {
		roleBinding := map[string]string{}
		for _, regionInfo := range strings.Split(roleResourceDefinitionString, util.Separator) {
			kvp := strings.Split(regionInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize(
						"Incorrect format in role resource definition description.\n",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "role-uuid":
				if len(strings.TrimSpace(val)) != 0 {
					roleBinding["role-uuid"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "resource-type":
				if len(strings.TrimSpace(val)) != 0 {
					roleBinding["resource-type"] = strings.ToUpper(val)
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "allow-all":
				if len(strings.TrimSpace(val)) != 0 {
					roleBinding["allow-all"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			case "resource-uuid":
				if len(strings.TrimSpace(val)) != 0 {
					roleBinding["resource-uuid"] = val
				} else {
					providerutil.ValueNotFoundForKeyError(key)
				}
			}
			if _, ok := roleBinding["role-uuid"]; !ok {
				logrus.Fatalln(
					formatter.Colorize(
						"Role UUID not specified in role resource definition description.\n",
						formatter.RedColor))
			}

			if _, ok := roleBinding["resource-type"]; !ok {
				logrus.Fatalln(
					formatter.Colorize(
						"Resource type not specified in role resource definition description.\n",
						formatter.RedColor))
			}

			allowAll, err := strconv.ParseBool(roleBinding["allow-all"])
			if err != nil {
				errMessage := err.Error() + " Setting default as false\n"
				logrus.Errorln(
					formatter.Colorize(errMessage, formatter.YellowColor),
				)
				allowAll = false
			}
			resourceUUIDs := strings.Split(roleBinding["resource-uuid"], ",")
			if len(resourceUUIDs) == 0 {
				allowAll = true
			}

			resourceDefinition := ybaclient.ResourceDefinition{
				AllowAll:        util.GetBoolPointer(allowAll),
				ResourceType:    util.GetStringPointer(roleBinding["resource-type"]),
				ResourceUUIDSet: &resourceUUIDs,
			}
			exists := false
			for i, value := range res {
				if strings.Compare(value.GetRoleUUID(), roleBinding["role-uuid"]) == 0 {
					exists = true
					valueResourceGroup := value.GetResourceGroup()
					valueResourceDefinitionSet := valueResourceGroup.GetResourceDefinitionSet()
					valueResourceDefinitionSet = append(valueResourceDefinitionSet, resourceDefinition)
					valueResourceGroup.SetResourceDefinitionSet(valueResourceDefinitionSet)
					value.SetResourceGroup(valueResourceGroup)
					res[i] = value
					break
				}
			}
			if !exists {
				resourceGroup := ybaclient.ResourceGroup{
					ResourceDefinitionSet: []ybaclient.ResourceDefinition{resourceDefinition},
				}
				roleResourceDefinition := ybaclient.RoleResourceDefinition{
					RoleUUID:      roleBinding["role-uuid"],
					ResourceGroup: &resourceGroup,
				}
				res = append(res, roleResourceDefinition)
			}
		}
	}
	return res
}
