/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetListOfCertificates fetches list of certificates associated with the customer
func (a *AuthAPIClient) GetListOfCertificates() (
	ybaclient.CertificateInfoApiApiGetListOfCertificateRequest) {
	return a.APIClient.CertificateInfoApi.GetListOfCertificate(a.ctx, a.CustomerUUID)
}

// DeleteCertificate - delete certificate
func (a *AuthAPIClient) DeleteCertificate(certUUID string) (
	ybaclient.CertificateInfoApiApiDeleteCertificateRequest) {
	return a.APIClient.CertificateInfoApi.DeleteCertificate(a.ctx, a.CustomerUUID, certUUID)
}
