/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetAZUTypes - get AZU types
func (a *AuthAPIClient) GetAZUTypes() ybaclient.InstanceTypesApiApiGetAZUTypesRequest {
	return a.APIClient.InstanceTypesApi.GetAZUTypes(a.ctx)
}

// GetEBSTypes - get EBS types
func (a *AuthAPIClient) GetEBSTypes() ybaclient.InstanceTypesApiApiGetEBSTypesRequest {
	return a.APIClient.InstanceTypesApi.GetEBSTypes(a.ctx)
}

// GetGCPTypes - get GCP instance types
func (a *AuthAPIClient) GetGCPTypes() ybaclient.InstanceTypesApiApiGetGCPTypesRequest {
	return a.APIClient.InstanceTypesApi.GetGCPTypes(a.ctx)
}

// GetGFlagMetadata - get GFlag metadata
func (a *AuthAPIClient) GetGFlagMetadata(
	version string,
) ybaclient.GFlagsValidationAPIsApiApiGetGFlagMetadataRequest {
	return a.APIClient.GFlagsValidationAPIsApi.GetGFlagMetadata(a.ctx, version)
}

// ListGFlags - list GFlags
func (a *AuthAPIClient) ListGFlags(
	version string,
) ybaclient.GFlagsValidationAPIsApiApiListGFlagsRequest {
	return a.APIClient.GFlagsValidationAPIsApi.ListGFlags(a.ctx, version)
}

// ValidateGFlags - validate GFlags
func (a *AuthAPIClient) ValidateGFlags(
	version string,
) ybaclient.GFlagsValidationAPIsApiApiValidateGFlagsRequest {
	return a.APIClient.GFlagsValidationAPIsApi.ValidateGFlags(a.ctx, version)
}
