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

// GetCertificate fetches the certificate UUID based on the label
func (a *AuthAPIClient) GetCertificate(label string) (
	ybaclient.CertificateInfoApiApiGetCertificateRequest,
) {
	return a.APIClient.CertificateInfoApi.GetCertificate(a.ctx, a.CustomerUUID, label)
}
