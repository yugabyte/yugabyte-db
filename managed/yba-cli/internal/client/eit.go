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
