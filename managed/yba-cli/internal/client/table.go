/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetAllTables for the listing of all tables in the universe
func (a *AuthAPIClient) GetAllTables(
	uUUID string,
) ybaclient.TableManagementApiApiGetAllTablesRequest {
	return a.APIClient.TableManagementApi.GetAllTables(a.ctx, a.CustomerUUID, uUUID)
}

// DescribeTable for the description of a table in the universe
func (a *AuthAPIClient) DescribeTable(
	uUUID, tUUID string,
) ybaclient.TableManagementApiApiDescribeTableRequest {
	return a.APIClient.TableManagementApi.DescribeTable(a.ctx, a.CustomerUUID, uUUID, tUUID)
}

// GetAllNamespaces for the listing of all namespaces in the universe
func (a *AuthAPIClient) GetAllNamespaces(
	uUUID string,
) ybaclient.TableManagementApiApiGetAllNamespacesRequest {
	return a.APIClient.TableManagementApi.GetAllNamespaces(a.ctx, a.CustomerUUID, uUUID)
}

// GetAllTableSpaces for the listing of all table spaces in the universe
func (a *AuthAPIClient) GetAllTableSpaces(
	uUUID string,
) ybaclient.TableManagementApiApiGetAllTableSpacesRequest {
	return a.APIClient.TableManagementApi.GetAllTableSpaces(a.ctx, a.CustomerUUID, uUUID)
}
