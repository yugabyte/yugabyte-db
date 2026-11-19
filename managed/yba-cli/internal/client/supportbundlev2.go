/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"fmt"
	"net/http"

	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

// The v2 wrappers carry a V2 suffix because AuthAPIClient already exposes the v1 support bundle
// calls under the unsuffixed names (see supportbundle.go).

// The generated name does not fit the 120 column limit at the point of use.
type estimateYbaSupportBundleSizeV2Request = ybav2client.SupportBundleAPIEstimateYbaSupportBundleSizeRequest

// CreateSupportBundleV2 creates a support bundle on a universe
func (a *AuthAPIClient) CreateSupportBundleV2(
	uUUID string,
) ybav2client.SupportBundleAPICreateSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.CreateSupportBundle(a.ctx, a.CustomerUUID, uUUID)
}

// CreateYbaSupportBundleV2 creates a YBA-only support bundle on the platform host
func (a *AuthAPIClient) CreateYbaSupportBundleV2() ybav2client.SupportBundleAPICreateYbaSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.CreateYbaSupportBundle(a.ctx, a.CustomerUUID)
}

// EstimateSupportBundleSizeV2 estimates support bundle size on a universe
func (a *AuthAPIClient) EstimateSupportBundleSizeV2(
	uUUID string,
) ybav2client.SupportBundleAPIEstimateSupportBundleSizeRequest {
	return a.APIv2Client.SupportBundleAPI.EstimateSupportBundleSize(a.ctx, a.CustomerUUID, uUUID)
}

// EstimateYbaSupportBundleSizeV2 estimates YBA-only support bundle size on the platform host
func (a *AuthAPIClient) EstimateYbaSupportBundleSizeV2() estimateYbaSupportBundleSizeV2Request {
	return a.APIv2Client.SupportBundleAPI.EstimateYbaSupportBundleSize(a.ctx, a.CustomerUUID)
}

// GetSupportBundleV2 fetches a support bundle on a universe
func (a *AuthAPIClient) GetSupportBundleV2(
	uUUID string,
	sbUUID string,
) ybav2client.SupportBundleAPIGetSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.GetSupportBundle(a.ctx, a.CustomerUUID, uUUID, sbUUID)
}

// GetYbaSupportBundleV2 fetches a YBA-only support bundle
func (a *AuthAPIClient) GetYbaSupportBundleV2(
	sbUUID string,
) ybav2client.SupportBundleAPIGetYbaSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.GetYbaSupportBundle(a.ctx, a.CustomerUUID, sbUUID)
}

// DeleteSupportBundleV2 deletes a support bundle on a universe
func (a *AuthAPIClient) DeleteSupportBundleV2(
	uUUID string,
	sbUUID string,
) ybav2client.SupportBundleAPIDeleteSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.DeleteSupportBundle(a.ctx, a.CustomerUUID, uUUID, sbUUID)
}

// DeleteYbaSupportBundleV2 deletes a YBA-only support bundle
func (a *AuthAPIClient) DeleteYbaSupportBundleV2(
	sbUUID string,
) ybav2client.SupportBundleAPIDeleteYbaSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.DeleteYbaSupportBundle(a.ctx, a.CustomerUUID, sbUUID)
}

// DownloadSupportBundleV2 downloads a support bundle archive from a universe. The operation is
// marked x-yba-api-stream-response, so Execute hands back the response with its body unread: the
// caller streams the archive from it and owns closing it.
func (a *AuthAPIClient) DownloadSupportBundleV2(
	uUUID string,
	sbUUID string,
) ybav2client.SupportBundleAPIDownloadSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.DownloadSupportBundle(a.ctx, a.CustomerUUID, uUUID, sbUUID)
}

// DownloadYbaSupportBundleV2 downloads a YBA-only support bundle archive. As with
// DownloadSupportBundleV2, the caller streams from and closes the response body.
func (a *AuthAPIClient) DownloadYbaSupportBundleV2(
	sbUUID string,
) ybav2client.SupportBundleAPIDownloadYbaSupportBundleRequest {
	return a.APIv2Client.SupportBundleAPI.DownloadYbaSupportBundle(a.ctx, a.CustomerUUID, sbUUID)
}

// The two paged-list calls stay on raw REST: the generated MarshalJSON drops fields an allOf
// inherits, so offset, limit and direction never reach the wire - for every allOf model, not just
// this one. The schema is fine; client/go/templates/model_simple.mustache keys its parent-merge on
// {{parent}} while the embedding comes from {{parentModel.name}}. Fix that, then use PageListSupportBundles.

// ListSupportBundleV2Raw fetches a page of support bundles for a universe
func (a *AuthAPIClient) ListSupportBundleV2Raw(
	uUUID string,
	body []byte,
) ([]byte, error) {
	return a.RestAPICallV2(RestAPIParameters{
		reqBytes:        body,
		method:          http.MethodPost,
		urlRoute:        fmt.Sprintf("universes/%s/support-bundles/page", uUUID),
		operationString: "List Support Bundles",
	})
}

// ListSupportBundleYbaV2Raw fetches a page of YBA-only support bundles
func (a *AuthAPIClient) ListSupportBundleYbaV2Raw(body []byte) ([]byte, error) {
	return a.RestAPICallV2(RestAPIParameters{
		reqBytes:        body,
		method:          http.MethodPost,
		urlRoute:        "support-bundles/yba/page",
		operationString: "List YBA Support Bundles",
	})
}
