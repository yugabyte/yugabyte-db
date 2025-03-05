/*
 * Copyright (c) YugaByte, Inc.
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
func (a *AuthAPIClient) ListPermissions() ybaclient.RBACManagementApiApiListPermissionsRequest {
	return a.APIClient.RBACManagementApi.ListPermissions(a.ctx, a.CustomerUUID)
}

// GetRole fetches role
func (a *AuthAPIClient) GetRole(rUUID string) ybaclient.RBACManagementApiApiGetRoleRequest {
	return a.APIClient.RBACManagementApi.GetRole(a.ctx, a.CustomerUUID, rUUID)
}

// CreateRole creates role
func (a *AuthAPIClient) CreateRole() ybaclient.RBACManagementApiApiCreateRoleRequest {
	return a.APIClient.RBACManagementApi.CreateRole(a.ctx, a.CustomerUUID)
}

// DeleteRole deletes role
func (a *AuthAPIClient) DeleteRole(rUUID string) ybaclient.RBACManagementApiApiDeleteRoleRequest {
	return a.APIClient.RBACManagementApi.DeleteRole(a.ctx, a.CustomerUUID, rUUID)
}

// EditRole edits role
func (a *AuthAPIClient) EditRole(rUUID string) ybaclient.RBACManagementApiApiEditRoleRequest {
	return a.APIClient.RBACManagementApi.EditRole(a.ctx, a.CustomerUUID, rUUID)
}

// ListRoles fetches list of roles
func (a *AuthAPIClient) ListRoles() ybaclient.RBACManagementApiApiListRolesRequest {
	return a.APIClient.RBACManagementApi.ListRoles(a.ctx, a.CustomerUUID)
}

// SetRoleBinding sets role binding
func (a *AuthAPIClient) SetRoleBinding(
	userUUID string,
) ybaclient.RBACManagementApiApiSetRoleBindingRequest {
	return a.APIClient.RBACManagementApi.SetRoleBinding(a.ctx, a.CustomerUUID, userUUID)
}

// GetRoleBindings fetches role bindings
func (a *AuthAPIClient) GetRoleBindings() ybaclient.RBACManagementApiApiGetRoleBindingsRequest {
	return a.APIClient.RBACManagementApi.GetRoleBindings(a.ctx, a.CustomerUUID)
}

// ListRoleBindingRest uses REST API to call list role binding functionality
func (a *AuthAPIClient) ListRoleBindingRest(
	userUUID, operation string,
) (
	map[string]([]ybaclient.RoleBinding), error,
) {
	token := viper.GetString("apiToken")
	errorTag := fmt.Errorf("RBAC: Role Bindings, Operation: %s", operation)

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
