/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetAllTables for the listing of all tables in the universe
func (a *AuthAPIClient) GetAllTables(
	uUUID string,
) ybaclient.TableManagementAPIGetAllTablesRequest {
	return a.APIClient.TableManagementAPI.GetAllTables(a.ctx, a.CustomerUUID, uUUID)
}

// DescribeTable for the description of a table in the universe
func (a *AuthAPIClient) DescribeTable(
	uUUID, tUUID string,
) ybaclient.TableManagementAPIDescribeTableRequest {
	return a.APIClient.TableManagementAPI.DescribeTable(a.ctx, a.CustomerUUID, uUUID, tUUID)
}

// GetAllNamespaces for the listing of all namespaces in the universe
func (a *AuthAPIClient) GetAllNamespaces(
	uUUID string,
) ybaclient.TableManagementAPIGetAllNamespacesRequest {
	return a.APIClient.TableManagementAPI.GetAllNamespaces(a.ctx, a.CustomerUUID, uUUID)
}

// GetAllTableSpaces for the listing of all table spaces in the universe
func (a *AuthAPIClient) GetAllTableSpaces(
	uUUID string,
) ybaclient.TableManagementAPIGetAllTableSpacesRequest {
	return a.APIClient.TableManagementAPI.GetAllTableSpaces(a.ctx, a.CustomerUUID, uUUID)
}
