/*
 * Copyright (c) YugabyteDB, Inc.
 */

package rbacutil

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"

	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// RBACRuntimeConfigurationCheck - Check if RBAC is enabled
func RBACRuntimeConfigurationCheck(
	authAPI *ybaAuthClient.AuthAPIClient,
	commandCall, operation string,
) (bool, error) {
	key := "yb.rbac.use_new_authz"
	configs, response, err := authAPI.ListFeatureFlags().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			commandCall, operation+" - List Feature Flags")
		return false, errMessage
	}
	rbacAllow := ""
	for _, config := range configs {
		if strings.Compare(config.GetKey(), key) == 0 {
			rbacAllow = config.GetValue()
			break
		}
	}
	rbacAllowBool, err := strconv.ParseBool(rbacAllow)
	if err != nil {
		return false, err
	}
	return rbacAllowBool, nil

}

// CheckRBACEnablementOnYBA - Allow RBAC commands
func CheckRBACEnablementOnYBA(
	authAPI *ybaAuthClient.AuthAPIClient,
	commandCall, operation string) {
	rbacAllow, err := RBACRuntimeConfigurationCheck(authAPI, commandCall, operation)
	if err != nil {
		logrus.Fatalf(err.Error())
	}
	if !rbacAllow {
		logrus.Fatalf(
			formatter.Colorize(
				"RBAC is not enabled in YugabyteDB Anywhere. "+
					"Please enable `yb.rbac.use_new_authz` runtime configuration to use rbac commands\n.",
				formatter.RedColor),
		)
	}
}

// RoleResourceDefinition - Structure to hold role resource definition
// This structure is used to parse the role resource definition string
// and to build the role resource definition object
type RoleResourceDefinition struct {
	RoleUUID      string
	ResourceType  string
	ResourceUUIDs []string
	AllowAll      bool
}

// parseroleResourceDefinitionString - Parse the role resource definition string
func parseroleResourceDefinitionString(roleResourceDefinitionString string) RoleResourceDefinition {
	roleResourceDefinition := RoleResourceDefinition{}
	for _, roleInfo := range strings.Split(roleResourceDefinitionString, util.Separator) {
		kvp := strings.Split(roleInfo, "=")
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
			if !util.IsEmptyString(val) {
				roleResourceDefinition.RoleUUID = val
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		case "resource-type":
			if !util.IsEmptyString(val) {
				roleResourceDefinition.ResourceType = strings.ToUpper(val)
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		case "allow-all":
			if !util.IsEmptyString(val) {
				var err error
				roleResourceDefinition.AllowAll, err = strconv.ParseBool(val)
				if err != nil {
					errMessage := err.Error() +
						" Invalid value provided for 'allow-all'. Setting it to 'false'.\n"
					logrus.Errorln(
						formatter.Colorize(errMessage, formatter.YellowColor),
					)
					roleResourceDefinition.AllowAll = false
				}
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		case "resource-uuid":
			if !util.IsEmptyString(val) {
				resourceUUIDs := strings.Split(val, ",")
				for i := 0; i < len(resourceUUIDs); i++ {
					resourceUUID := strings.TrimSpace(resourceUUIDs[i])
					if len(resourceUUID) != 0 {
						roleResourceDefinition.ResourceUUIDs = append(
							roleResourceDefinition.ResourceUUIDs,
							resourceUUID,
						)
					} else {
						providerutil.ValueNotFoundForKeyError(key)
					}
				}
			} else {
				providerutil.ValueNotFoundForKeyError(key)
			}
		default:
			logrus.Fatalln(
				formatter.Colorize(
					fmt.Sprintf("Unknown key %s in role resource definition description.\n", key),
					formatter.RedColor))
		}
	}
	if roleResourceDefinition.RoleUUID == "" {
		logrus.Fatalln(
			formatter.Colorize(
				"Role UUID not specified in role resource definition description.\n",
				formatter.RedColor))
	}
	if len(roleResourceDefinition.ResourceUUIDs) == 0 {
		roleResourceDefinition.AllowAll = true
	}
	return roleResourceDefinition
}

// validateRoleResourceDefinitionForSytemRoles - Validate role resource definition for system roles
func validateRoleResourceDefinitionForSytemRoles(roleResourceDefinition RoleResourceDefinition) {
	if len(roleResourceDefinition.ResourceUUIDs) > 0 {
		logrus.Fatalln(
			formatter.Colorize(
				"System role cannot have resource uuids\n",
				formatter.RedColor))
	}
	if len(roleResourceDefinition.ResourceType) > 0 {
		logrus.Fatalln(
			formatter.Colorize(
				"System role cannot have resource type\n",
				formatter.RedColor))
	}
}

// IsSystemRole - Check if the role is a system role
func IsSystemRole(
	authAPI *ybaAuthClient.AuthAPIClient,
	roleUUID string,
	callsite string,
) bool {
	role, response, err := authAPI.GetRole(roleUUID).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			callsite,
			"Get Role",
		)
		logrus.Fatalln(
			formatter.Colorize(
				errMessage.Error()+"\n",
				formatter.RedColor))
	}
	return role.GetRoleType() == util.SystemRoleType
}

// BuildResourceRoleDefinition - Build resource role definition
func BuildResourceRoleDefinition(
	authAPI *ybaAuthClient.AuthAPIClient,
	roleResourceDefinitionStrings []string) []ybaclient.RoleResourceDefinition {
	if len(roleResourceDefinitionStrings) == 0 {
		return nil
	}
	res := make([]ybaclient.RoleResourceDefinition, 0)

	for _, roleResourceDefinitionString := range roleResourceDefinitionStrings {
		roleResourceDefinition := parseroleResourceDefinitionString(roleResourceDefinitionString)
		systemRole := IsSystemRole(authAPI, roleResourceDefinition.RoleUUID, "RBAC: Role Binding")
		if systemRole {
			validateRoleResourceDefinitionForSytemRoles(roleResourceDefinition)
		}

		resourceDefinition := ybaclient.ResourceDefinition{
			AllowAll:        util.GetBoolPointer(roleResourceDefinition.AllowAll),
			ResourceType:    util.GetStringPointer(roleResourceDefinition.ResourceType),
			ResourceUUIDSet: roleResourceDefinition.ResourceUUIDs,
		}

		exists := false
		for i, value := range res {
			if strings.Compare(value.GetRoleUUID(), roleResourceDefinition.RoleUUID) == 0 {
				exists = true
				if !systemRole {
					valueResourceGroup := value.GetResourceGroup()
					valueResourceDefinitionSet := valueResourceGroup.GetResourceDefinitionSet()
					valueResourceDefinitionSet = append(
						valueResourceDefinitionSet,
						resourceDefinition,
					)
					valueResourceGroup.SetResourceDefinitionSet(valueResourceDefinitionSet)
					value.SetResourceGroup(valueResourceGroup)
				} else {
					value.ResourceGroup = nil
				}
				res[i] = value
				break
			}
		}
		if !exists {
			resourceGroup := ybaclient.ResourceGroup{
				ResourceDefinitionSet: []ybaclient.ResourceDefinition{resourceDefinition},
			}
			roleResourceDefinition := ybaclient.RoleResourceDefinition{
				RoleUUID: roleResourceDefinition.RoleUUID,
			}
			if !systemRole {
				roleResourceDefinition.SetResourceGroup(resourceGroup)
			} else {
				roleResourceDefinition.ResourceGroup = nil
			}
			res = append(res, roleResourceDefinition)
		}
	}
	return res
}

// BuildResourceRoleDefinitionV2 - Build resource role definition for v2
// This function is used to build the role resource definition for v2 APIs
// used in group mapping
func BuildResourceRoleDefinitionV2(
	authAPI *ybaAuthClient.AuthAPIClient,
	roleResourceDefinitionStrings []string,
	existingRoleResourceDefinitions []ybav2client.RoleResourceDefinition,
) []ybav2client.RoleResourceDefinition {
	if len(roleResourceDefinitionStrings) == 0 {
		return nil
	}

	if existingRoleResourceDefinitions == nil {
		existingRoleResourceDefinitions = make([]ybav2client.RoleResourceDefinition, 0)
	}

	for _, roleResourceDefinitionString := range roleResourceDefinitionStrings {
		roleResourceDefinition := parseroleResourceDefinitionString(roleResourceDefinitionString)
		systemRole := IsSystemRole(
			authAPI,
			roleResourceDefinition.RoleUUID,
			"Authentication: Group Mapping",
		)
		if systemRole {
			validateRoleResourceDefinitionForSytemRoles(roleResourceDefinition)
		}

		resourceDefinition := ybav2client.ResourceDefinition{
			AllowAll:        roleResourceDefinition.AllowAll,
			ResourceType:    roleResourceDefinition.ResourceType,
			ResourceUuidSet: roleResourceDefinition.ResourceUUIDs,
		}

		exists := false
		for i, value := range existingRoleResourceDefinitions {
			if strings.Compare(value.GetRoleUuid(), roleResourceDefinition.RoleUUID) == 0 {
				exists = true
				if !systemRole {
					valueResourceGroup := value.GetResourceGroup()
					valueResourceDefinitionSet := valueResourceGroup.GetResourceDefinitionSet()
					valueResourceDefinitionSet = append(
						valueResourceDefinitionSet,
						resourceDefinition,
					)
					valueResourceGroup.SetResourceDefinitionSet(valueResourceDefinitionSet)
					value.SetResourceGroup(valueResourceGroup)
				} else {
					value.ResourceGroup = nil
				}
				existingRoleResourceDefinitions[i] = value
				break
			}
		}
		if !exists {
			resourceGroup := ybav2client.ResourceGroup{
				ResourceDefinitionSet: []ybav2client.ResourceDefinition{resourceDefinition},
			}
			roleResourceDefinitionV2 := ybav2client.RoleResourceDefinition{
				RoleUuid: roleResourceDefinition.RoleUUID,
			}
			if !systemRole {
				roleResourceDefinitionV2.SetResourceGroup(resourceGroup)
			} else {
				roleResourceDefinitionV2.ResourceGroup = nil
			}
			existingRoleResourceDefinitions = append(
				existingRoleResourceDefinitions,
				roleResourceDefinitionV2,
			)
		}
	}
	return existingRoleResourceDefinitions
}

// ModifyExistingRoleResourceDefinitionV2 - Modify existing role resource definition list
func ModifyExistingRoleResourceDefinitionV2(
	authAPI *ybaAuthClient.AuthAPIClient,
	roleResourceDefinitionStrings []string,
	existingRoleResourceDefinitions []ybav2client.RoleResourceDefinition,
) []ybav2client.RoleResourceDefinition {
	return BuildResourceRoleDefinitionV2(
		authAPI,
		roleResourceDefinitionStrings,
		existingRoleResourceDefinitions,
	)
}

// BuildNewRoleResourceDefinitionV2 - Build new role resource definition list
func BuildNewRoleResourceDefinitionV2(
	authAPI *ybaAuthClient.AuthAPIClient,
	roleResourceDefinitionStrings []string) []ybav2client.RoleResourceDefinition {
	return BuildResourceRoleDefinitionV2(authAPI, roleResourceDefinitionStrings, nil)
}
