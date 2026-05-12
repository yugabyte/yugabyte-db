/*
 * Copyright (c) YugabyteDB, Inc.
 */

package group

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/rbac/rbacutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	groupmapping "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/groups"
)

// UpdateParams holds the parameters for updating a group
type UpdateParams struct {
	groupName                 string
	roleResourceDefinition    []string
	addRoleResourceDefinition []string
	removeRoles               []string
}

// ListGroupMappingUtil executes the list groupMapping command
func ListGroupMappingUtil(groupMappingCode string, groupMappingName string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	groupList := getGroupList(authAPI, groupMappingCode, groupMappingName)
	printGroupList(authAPI, groupList)
}

// CreateGroupUtil executes the create groupMapping command
func CreateGroupUtil(cmd *cobra.Command, groupName string, roles []string, authCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	groupList := getGroupList(authAPI, "", groupName)
	if len(groupList) > 0 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf(
				"Group %s already exists. Please use update command to update it.\n",
				groupName,
			),
			formatter.RedColor,
		))
	}
	roleDefinitions := rbacutil.BuildNewRoleResourceDefinitionV2(authAPI, roles)
	requestBody := []ybav2client.AuthGroupToRolesMapping{
		*ybav2client.NewAuthGroupToRolesMapping(groupName, authCode, roleDefinitions),
	}
	response, err := authAPI.UpdateGroupMappings().AuthGroupToRolesMapping(requestBody).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Group", "Create")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	logrus.Info(
		fmt.Sprintf(
			"Group %s created successfully\n",
			formatter.Colorize(groupName, formatter.GreenColor),
		),
	)
	groupList = getGroupList(authAPI, "", groupName)
	printGroupList(authAPI, groupList)
}

// UpdateGroupUtil executes the update groupMapping command
func UpdateGroupUtil(params UpdateParams) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	groupToUpdate := getGroupList(authAPI, "", params.groupName)
	if len(groupToUpdate) < 1 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf("Group %s not found\n", params.groupName),
			formatter.RedColor))
		return
	}
	if len(groupToUpdate) > 1 {
		logrus.Warn(formatter.Colorize(
			"Multiple groups found with the same name. Only first one will be updated.\n",
			formatter.YellowColor))
	}
	if len(params.roleResourceDefinition) > 0 {
		groupToUpdate[0].SetRoleResourceDefinitions(
			rbacutil.BuildNewRoleResourceDefinitionV2(authAPI, params.roleResourceDefinition))
	} else {
		if len(params.removeRoles) > 0 {
			existingRoles := groupToUpdate[0].GetRoleResourceDefinitions()
			roleSetToRemove := make(map[string]struct{}, len(params.removeRoles))
			for _, roleToRemove := range params.removeRoles {
				roleSetToRemove[roleToRemove] = struct{}{}
			}
			filteredRoles := existingRoles[:0]
			for _, role := range existingRoles {
				if _, found := roleSetToRemove[role.GetRoleUuid()]; !found {
					if rbacutil.IsSystemRole(authAPI, role.GetRoleUuid(), "Authentication: Group Mapping") {
						role.ResourceGroup = nil
					}
					filteredRoles = append(filteredRoles, role)
				}
			}
			groupToUpdate[0].SetRoleResourceDefinitions(filteredRoles)
		}
		if len(params.addRoleResourceDefinition) > 0 {
			groupToUpdate[0].SetRoleResourceDefinitions(
				rbacutil.ModifyExistingRoleResourceDefinitionV2(
					authAPI,
					params.addRoleResourceDefinition,
					groupToUpdate[0].GetRoleResourceDefinitions(),
				),
			)
		}
	}
	// Reset the creation date
	groupToUpdate[0].CreationDate = nil
	response, err := authAPI.UpdateGroupMappings().AuthGroupToRolesMapping(groupToUpdate).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Group", "Update")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	logrus.Info(
		fmt.Sprintf(
			"Group %s updated successfully\n",
			formatter.Colorize(params.groupName, formatter.GreenColor),
		),
	)
	groupList := getGroupList(authAPI, "", params.groupName)
	printGroupList(authAPI, groupList)
}

// DeleteGroupUtil executes the delete groupMapping command
func DeleteGroupUtil(cmd *cobra.Command, groupName string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	groupList := getGroupList(authAPI, "", groupName)

	if len(groupList) < 1 {
		logrus.Fatal(formatter.Colorize(
			fmt.Sprintf("Group %s not found\n", groupName),
			formatter.RedColor))
		return
	}
	if len(groupList) > 1 {
		logrus.Warn(formatter.Colorize(
			"Multiple groups found with the same name. Only first one will be updated.\n",
			formatter.YellowColor))
	}

	// Delete the group
	response, err := authAPI.DeleteGroupMappings(groupList[0].GetUuid()).Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Group", "Delete")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	logrus.Info(
		fmt.Sprintf(
			"Group %s deleted successfully\n",
			formatter.Colorize(groupName, formatter.GreenColor),
		),
	)
}

// Utility functions
func getGroupList(
	authAPI *ybaAuthClient.AuthAPIClient,
	groupMappingCode string,
	groupMappingName string,
) []ybav2client.AuthGroupToRolesMapping {
	rList, response, err := authAPI.ListMappings().Execute()
	if err != nil {
		callSite := "Group"
		errMessage := util.ErrorFromHTTPResponse(response, err, callSite, "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	// Return the full list if both name and code are empty
	if util.IsEmptyString(groupMappingName) &&
		util.IsEmptyString(groupMappingCode) {
		return rList
	}
	// Filter by name and/or by groupMapping code
	var r []ybav2client.AuthGroupToRolesMapping
	for _, groupMapping := range rList {
		if (util.IsEmptyString(groupMappingName) ||
			strings.Compare(groupMapping.GetGroupIdentifier(), groupMappingName) == 0) &&
			(util.IsEmptyString(groupMappingCode) || strings.Compare(groupMapping.GetType(), groupMappingCode) == 0) {
			r = append(r, groupMapping)
		}
	}
	return r
}

func printGroupList(
	authAPI *ybaAuthClient.AuthAPIClient,
	groupList []ybav2client.AuthGroupToRolesMapping) {
	roleListRequest := authAPI.ListRoles()
	rolesList, response, err := roleListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err, "Role", "List")
		logrus.Fatalf("%s\n", formatter.Colorize(errMessage.Error(), formatter.RedColor))
	}
	roleMap := make(map[string]string)
	for _, availableRole := range rolesList {
		roleMap[availableRole.GetRoleUUID()] = strings.TrimSpace(availableRole.GetName())
	}
	groupMappingCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  groupmapping.NewGroupMappingFormat(viper.GetString("output")),
	}
	if len(groupList) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Info("No group mappings found\n")
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	groupmapping.Write(groupMappingCtx, groupList, &roleMap)
}
