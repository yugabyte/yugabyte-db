// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.NOT_FOUND;

import com.oracle.bmc.core.model.Image;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.oci.OCICloudImpl;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

public class ImageBundleUtilTest extends FakeDBApplication {

  private static final String REGION = "us-ashburn-1";
  private static final String MARKETPLACE_IMAGE = "ocid1.image.oc1.iad.marketplace";
  private static final String PLATFORM_IMAGE = "ocid1.image.oc1.iad.platform";
  private static final String CUSTOM_IMAGE = "ocid1.image.oc1.iad.custom";

  private final OCICloudImpl mockOCICloudImpl = mock(OCICloudImpl.class);
  private Customer customer;
  private Provider ociProvider;
  private Region ociRegion;
  private ImageBundleUtil imageBundleUtil;

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(builder)
        .overrides(bind(OCICloudImpl.class).toInstance(mockOCICloudImpl));
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    ociProvider = ModelFactory.ociProvider(customer);
    ociRegion = Region.create(ociProvider, REGION, REGION, null);
    imageBundleUtil = app.injector().instanceOf(ImageBundleUtil.class);
    stubImage(MARKETPLACE_IMAGE, "publisherCompartment");
    stubImage(PLATFORM_IMAGE, null);
    stubImage(CUSTOM_IMAGE, "ocid1.compartment.oc1..custom");
  }

  private void stubImage(String imageId, String compartmentId) {
    when(mockOCICloudImpl.getImageOrBadRequest(any(Provider.class), eq(REGION), eq(imageId)))
        .thenReturn(Image.builder().id(imageId).compartmentId(compartmentId).build());
  }

  private static ImageBundleDetails details(String ybImage, Boolean isImageMarketplaceBased) {
    BundleInfo info = new BundleInfo();
    info.setYbImage(ybImage);
    info.setIsImageMarketplaceBased(isImageMarketplaceBased);
    Map<String, BundleInfo> regions = new HashMap<>();
    regions.put(REGION, info);
    ImageBundleDetails details = new ImageBundleDetails();
    details.setArch(Architecture.x86_64);
    details.setRegions(regions);
    return details;
  }

  private static Boolean flag(ImageBundleDetails details) {
    return details.getRegions().get(REGION).getIsImageMarketplaceBased();
  }

  private void verifyLookups(String imageId, int count) {
    verify(mockOCICloudImpl, times(count)).getImageOrBadRequest(any(), eq(REGION), eq(imageId));
  }

  @Test
  public void testFlagComesFromOciNotClient() {
    ImageBundleDetails marketplace = details(MARKETPLACE_IMAGE, false);
    ImageBundleDetails platform = details(PLATFORM_IMAGE, true);
    ImageBundleDetails custom = details(CUSTOM_IMAGE, true);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, marketplace, null);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, platform, null);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, custom, null);
    assertEquals(true, flag(marketplace));
    assertEquals(false, flag(platform));
    assertEquals(false, flag(custom));
    verify(mockOCICloudImpl, times(1)).getMarketplaceBaseImageId(any(), any(), any());
  }

  @Test
  public void testCustomImageBuiltFromMarketplaceIsMarketplaceBased() {
    when(mockOCICloudImpl.getMarketplaceBaseImageId(
            any(Provider.class), eq(REGION), any(Image.class)))
        .thenReturn(MARKETPLACE_IMAGE);
    ImageBundleDetails custom = details(CUSTOM_IMAGE, false);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, custom, null);
    assertEquals(true, flag(custom));
  }

  @Test
  public void testFlagUnsetWhenBaseImageIsMissing() {
    when(mockOCICloudImpl.getMarketplaceBaseImageId(
            any(Provider.class), eq(REGION), any(Image.class)))
        .thenThrow(new PlatformServiceException(NOT_FOUND, "Image not found"));
    ImageBundleDetails custom = details(CUSTOM_IMAGE, true);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, custom, null);
    assertNull(flag(custom));
  }

  @Test
  public void testStoredFlagKeptForSameImage() {
    ImageBundleDetails request = details(MARKETPLACE_IMAGE, false);
    imageBundleUtil.setImageMarketplaceBasedFlags(
        ociProvider, request, details(MARKETPLACE_IMAGE, true));
    assertEquals(true, flag(request));
    verify(mockOCICloudImpl, never()).getImageOrBadRequest(any(), anyString(), anyString());
  }

  @Test
  public void testChangedImageIsLookedUp() {
    ImageBundleDetails request = details(PLATFORM_IMAGE, true);
    imageBundleUtil.setImageMarketplaceBasedFlags(
        ociProvider, request, details(MARKETPLACE_IMAGE, true));
    assertEquals(false, flag(request));
    verifyLookups(PLATFORM_IMAGE, 1);
  }

  @Test
  public void testMissingStoredFlagIsLookedUp() {
    ImageBundleDetails request = details(MARKETPLACE_IMAGE, null);
    imageBundleUtil.setImageMarketplaceBasedFlags(
        ociProvider, request, details(MARKETPLACE_IMAGE, null));
    assertEquals(true, flag(request));
    verifyLookups(MARKETPLACE_IMAGE, 1);
  }

  @Test
  public void testNoFlagWithoutImage() {
    ImageBundleDetails request = details(null, true);
    imageBundleUtil.setImageMarketplaceBasedFlags(ociProvider, request, null);
    assertNull(flag(request));
    verify(mockOCICloudImpl, never()).getImageOrBadRequest(any(), anyString(), anyString());
  }

  @Test
  public void testNoFlagForOtherClouds() {
    Provider awsProvider = ModelFactory.awsProvider(customer);
    ImageBundleDetails request = details("ami-123", true);
    imageBundleUtil.setImageMarketplaceBasedFlags(awsProvider, request, null);
    assertNull(flag(request));
    verify(mockOCICloudImpl, never()).getImageOrBadRequest(any(), anyString(), anyString());
  }

  @Test
  public void testLookupFailureFailsTheSave() {
    when(mockOCICloudImpl.getImageOrBadRequest(any(), eq(REGION), eq("ocid1.image.missing")))
        .thenThrow(new PlatformServiceException(NOT_FOUND, "Image not found"));
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                imageBundleUtil.setImageMarketplaceBasedFlags(
                    ociProvider, details("ocid1.image.missing", null), null));
    assertEquals(NOT_FOUND, e.getHttpStatus());
  }

  @Test
  public void testRegionSyncDoesNotSaveRequestFlag() {
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.disableImageBundleValidation.getKey(), "true");
    ImageBundle bundle =
        ImageBundle.create(ociProvider, "oci-bundle", details(MARKETPLACE_IMAGE, true), true);
    ImageBundle request = Json.fromJson(Json.toJson(bundle), ImageBundle.class);
    request.getDetails().getRegions().get(REGION).setIsImageMarketplaceBased(false);

    imageBundleUtil.updateImageBundleIfRequired(
        ociProvider, Collections.singletonList(ociRegion), request);

    BundleInfo stored = ImageBundle.get(bundle.getUuid()).getDetails().getRegions().get(REGION);
    assertEquals(MARKETPLACE_IMAGE, stored.getYbImage());
    assertEquals(true, stored.getIsImageMarketplaceBased());
    verify(mockOCICloudImpl, never()).getImageOrBadRequest(any(), anyString(), anyString());
  }
}
