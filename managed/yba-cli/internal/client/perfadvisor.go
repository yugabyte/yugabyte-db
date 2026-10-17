/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

// The generated name does not fit the 120 column limit at the point of use.
type validatePerfAdvisorEndpointRequest = ybav2client.PerfAdvisorEndpointAPIValidatePerfAdvisorEndpointRequest

// ListPerfAdvisorEndpoints lists the external Perf Advisor destinations
func (a *AuthAPIClient) ListPerfAdvisorEndpoints() ybav2client.PerfAdvisorEndpointAPIListPerfAdvisorEndpointsRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.ListPerfAdvisorEndpoints(a.ctx, a.CustomerUUID)
}

// GetPerfAdvisorEndpoint fetches one external Perf Advisor destination
func (a *AuthAPIClient) GetPerfAdvisorEndpoint(
	peUUID string,
) ybav2client.PerfAdvisorEndpointAPIGetPerfAdvisorEndpointRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.GetPerfAdvisorEndpoint(
		a.ctx, a.CustomerUUID, peUUID)
}

// CreatePerfAdvisorEndpoint creates an external Perf Advisor destination
func (a *AuthAPIClient) CreatePerfAdvisorEndpoint() ybav2client.PerfAdvisorEndpointAPICreatePerfAdvisorEndpointRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.CreatePerfAdvisorEndpoint(a.ctx, a.CustomerUUID)
}

// EditPerfAdvisorEndpoint edits an external Perf Advisor destination
func (a *AuthAPIClient) EditPerfAdvisorEndpoint(
	peUUID string,
) ybav2client.PerfAdvisorEndpointAPIEditPerfAdvisorEndpointRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.EditPerfAdvisorEndpoint(
		a.ctx, a.CustomerUUID, peUUID)
}

// DeletePerfAdvisorEndpoint deletes an external Perf Advisor destination
func (a *AuthAPIClient) DeletePerfAdvisorEndpoint(
	peUUID string,
) ybav2client.PerfAdvisorEndpointAPIDeletePerfAdvisorEndpointRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.DeletePerfAdvisorEndpoint(
		a.ctx, a.CustomerUUID, peUUID)
}

// ValidatePerfAdvisorEndpoint probes a destination without storing it
func (a *AuthAPIClient) ValidatePerfAdvisorEndpoint() validatePerfAdvisorEndpointRequest {
	return a.APIv2Client.PerfAdvisorEndpointAPI.ValidatePerfAdvisorEndpoint(a.ctx, a.CustomerUUID)
}

// ListAllPACollectors lists the Perf Advisor collectors configured for the customer
func (a *AuthAPIClient) ListAllPACollectors() ybaclient.PACollectorAPIListAllPACollectorsRequest {
	return a.APIClient.PACollectorAPI.ListAllPACollectors(a.ctx, a.CustomerUUID)
}

// RegisterUniverseWithPACollector registers a universe with a Perf Advisor collector
func (a *AuthAPIClient) RegisterUniverseWithPACollector(
	uUUID, paUUID string,
) ybaclient.PACollectorAPIRegisterUniverseRequest {
	return a.APIClient.PACollectorAPI.RegisterUniverse(a.ctx, a.CustomerUUID, uUUID, paUUID)
}

// UnregisterUniverseFromPACollector unregisters a universe from its Perf Advisor collector
func (a *AuthAPIClient) UnregisterUniverseFromPACollector(
	uUUID string,
) ybaclient.PACollectorAPIUnregisterUniverseRequest {
	return a.APIClient.PACollectorAPI.UnregisterUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// CheckUniversePARegistration reports how a universe is registered with Perf Advisor
func (a *AuthAPIClient) CheckUniversePARegistration(
	uUUID string,
) ybaclient.PACollectorAPICheckRegisteredRequest {
	return a.APIClient.PACollectorAPI.CheckRegistered(a.ctx, a.CustomerUUID, uUUID)
}
