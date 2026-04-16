/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"

	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListPermissions fetches list of permissions
func (a *AuthAPIClient) ListPermissions() ybaclient.RBACManagementAPIListPermissionsRequest {
	return a.APIClient.RBACManagementAPI.ListPermissions(a.ctx, a.CustomerUUID)
}

// GetRole fetches role
func (a *AuthAPIClient) GetRole(rUUID string) ybaclient.RBACManagementAPIGetRoleRequest {
	return a.APIClient.RBACManagementAPI.GetRole(a.ctx, a.CustomerUUID, rUUID)
}

// CreateRole creates role
func (a *AuthAPIClient) CreateRole() ybaclient.RBACManagementAPICreateRoleRequest {
	return a.APIClient.RBACManagementAPI.CreateRole(a.ctx, a.CustomerUUID)
}

// DeleteRole deletes role
func (a *AuthAPIClient) DeleteRole(rUUID string) ybaclient.RBACManagementAPIDeleteRoleRequest {
	return a.APIClient.RBACManagementAPI.DeleteRole(a.ctx, a.CustomerUUID, rUUID)
}

// EditRole edits role
func (a *AuthAPIClient) EditRole(rUUID string) ybaclient.RBACManagementAPIEditRoleRequest {
	return a.APIClient.RBACManagementAPI.EditRole(a.ctx, a.CustomerUUID, rUUID)
}

// ListRoles fetches list of roles
func (a *AuthAPIClient) ListRoles() ybaclient.RBACManagementAPIListRolesRequest {
	return a.APIClient.RBACManagementAPI.ListRoles(a.ctx, a.CustomerUUID)
}

// SetRoleBinding sets role binding
func (a *AuthAPIClient) SetRoleBinding(
	userUUID string,
) ybaclient.RBACManagementAPISetRoleBindingRequest {
	return a.APIClient.RBACManagementAPI.SetRoleBinding(a.ctx, a.CustomerUUID, userUUID)
}

// GetRoleBindings fetches role bindings
func (a *AuthAPIClient) GetRoleBindings() ybaclient.RBACManagementAPIGetRoleBindingsRequest {
	return a.APIClient.RBACManagementAPI.GetRoleBindings(a.ctx, a.CustomerUUID)
}

// ListRoleBindingRest uses REST API to call list role binding functionality
func (a *AuthAPIClient) ListRoleBindingRest(
	userUUID, command, operation string,
) (
	map[string]([]ybaclient.RoleBinding), error,
) {
	token := viper.GetString("apiToken")
	errorTag := fmt.Errorf("%s, Operation: %s", command, operation)

	var req *http.Request

	query := url.Values{}
	if len(strings.TrimSpace(userUUID)) > 0 {
		query.Add("userUUID", userUUID)
	}

	req, err := http.NewRequest(
		http.MethodGet,
		fmt.Sprintf("%s://%s/api/v1/customers/%s/rbac/role_binding",
			a.RestClient.Scheme, a.RestClient.Host, a.CustomerUUID),
		nil,
	)

	if err != nil {
		return nil,
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	req.URL.RawQuery = query.Encode()

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return nil,
			fmt.Errorf("%w: Error occured during POST call for list role binding %s",
				errorTag,
				err.Error())
	}

	var body []byte
	body, err = io.ReadAll(r.Body)
	if err != nil {
		return nil,
			fmt.Errorf("%w: Error reading list role binding response body %s",
				errorTag,
				err.Error())
	}

	var responseBody map[string]([]ybaclient.RoleBinding)
	if err = json.Unmarshal(body, &responseBody); err != nil {
		responseBodyError := util.YbaStructuredError{}
		if err = json.Unmarshal(body, &responseBodyError); err != nil {
			return nil,
				fmt.Errorf("%w: Failed unmarshalling list role binding error response body %s",
					errorTag,
					err.Error())
		}

		errorMessage := util.ErrorFromResponseBody(responseBodyError)
		return nil,
			fmt.Errorf("%w: Error fetching list of role bindings: %s", errorTag, errorMessage)
	}
	return responseBody, nil
}
