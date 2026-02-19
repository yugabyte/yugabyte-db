/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetAZUTypes - get AZU types
func (a *AuthAPIClient) GetAZUTypes() ybaclient.InstanceTypesAPIGetAZUTypesRequest {
	return a.APIClient.InstanceTypesAPI.GetAZUTypes(a.ctx)
}

// GetEBSTypes - get EBS types
func (a *AuthAPIClient) GetEBSTypes() ybaclient.InstanceTypesAPIGetEBSTypesRequest {
	return a.APIClient.InstanceTypesAPI.GetEBSTypes(a.ctx)
}

// GetGCPTypes - get GCP instance types
func (a *AuthAPIClient) GetGCPTypes() ybaclient.InstanceTypesAPIGetGCPTypesRequest {
	return a.APIClient.InstanceTypesAPI.GetGCPTypes(a.ctx)
}

// GetGFlagMetadata - get GFlag metadata
func (a *AuthAPIClient) GetGFlagMetadata(
	version string,
) ybaclient.GFlagsValidationAPIsAPIGetGFlagMetadataRequest {
	return a.APIClient.GFlagsValidationAPIsAPI.GetGFlagMetadata(a.ctx, version)
}

// ListGFlags - list GFlags
func (a *AuthAPIClient) ListGFlags(
	version string,
) ybaclient.GFlagsValidationAPIsAPIListGFlagsRequest {
	return a.APIClient.GFlagsValidationAPIsAPI.ListGFlags(a.ctx, version)
}

// ValidateGFlags - validate GFlags
func (a *AuthAPIClient) ValidateGFlags(
	version string,
) ybaclient.GFlagsValidationAPIsAPIValidateGFlagsRequest {
	return a.APIClient.GFlagsValidationAPIsAPI.ValidateGFlags(a.ctx, version)
}
