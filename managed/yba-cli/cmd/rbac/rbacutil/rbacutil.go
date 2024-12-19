/*
 * Copyright (c) YugaByte, Inc.
 */

package rbacutil

import (
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
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
	scopeUUID := "00000000-0000-0000-0000-000000000000" // global scope
	key := "yb.rbac.use_new_authz"
	rbacAllow, response, err := authAPI.GetConfigurationKey(scopeUUID, key).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			commandCall, operation+" - Get Runtime Configuration Key")
		return false, errMessage
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

// BuildResourceRoleDefinition - Build resource role definition
func BuildResourceRoleDefinition(
	roleResourceDefinitionStrings []string) []ybaclient.RoleResourceDefinition {
	if len(roleResourceDefinitionStrings) == 0 {
		return nil
	}
	res := make([]ybaclient.RoleResourceDefinition, 0)

	for _, roleResourceDefinitionString := range roleResourceDefinitionStrings {
		roleBinding := map[string]string{}
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
		if len(resourceUUIDs) == 1 && resourceUUIDs[0] == "" {
			resourceUUIDs = []string{}
		}
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
	return res
}
