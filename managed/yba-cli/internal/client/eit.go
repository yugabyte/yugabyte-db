/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetListOfCertificates fetches list of certificates associated with the customer
func (a *AuthAPIClient) GetListOfCertificates() ybaclient.CertificateInfoAPIGetListOfCertificateRequest {
	return a.APIClient.CertificateInfoAPI.GetListOfCertificate(a.ctx, a.CustomerUUID)
}

// DeleteCertificate - delete certificate
func (a *AuthAPIClient) DeleteCertificate(
	certUUID string,
) ybaclient.CertificateInfoAPIDeleteCertificateRequest {
	return a.APIClient.CertificateInfoAPI.DeleteCertificate(a.ctx, a.CustomerUUID, certUUID)
}

// Upload - upload certificate
func (a *AuthAPIClient) Upload() ybaclient.CertificateInfoAPIUploadRequest {
	return a.APIClient.CertificateInfoAPI.Upload(a.ctx, a.CustomerUUID)
}

// EditCertificate - edit certificate
func (a *AuthAPIClient) EditCertificate(
	certUUID string,
) ybaclient.CertificateInfoAPIEditCertificateRequest {
	return a.APIClient.CertificateInfoAPI.EditCertificate(a.ctx, a.CustomerUUID, certUUID)
}

// GetRootCert - get root certificate
func (a *AuthAPIClient) GetRootCert(
	certUUID string,
) ybaclient.CertificateInfoAPIGetRootCertRequest {
	return a.APIClient.CertificateInfoAPI.GetRootCert(a.ctx, a.CustomerUUID, certUUID)
}

// GetClientCert - get client certificate
func (a *AuthAPIClient) GetClientCert(
	certUUID string,
) ybaclient.CertificateInfoAPIGetClientCertRequest {
	return a.APIClient.CertificateInfoAPI.GetClientCert(a.ctx, a.CustomerUUID, certUUID)
}
