// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckOCIImageEligibility.BundleImage;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.ImageBundleDetails.BundleInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;

public class CheckOCIImageEligibilityTest extends FakeDBApplication {

  private static final String REGION = "us-ashburn-1";
  private static final String ALLOW_MARKETPLACE_BASED_KEY =
      UniverseConfKeys.ociAllowBootVolumeReplaceForMarketplaceBasedImages.getKey();

  private CheckOCIImageEligibility task;
  private ImageBundleUtil imageBundleUtil;
  private Provider provider;
  private Universe universe;
  private int bundleCount;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    provider = ModelFactory.ociProvider(customer);
    universe = ModelFactory.createUniverse(customer.getId());
    BaseTaskDependencies baseTaskDependencies = mock(BaseTaskDependencies.class);
    when(baseTaskDependencies.getConfGetter())
        .thenReturn(app.injector().instanceOf(RuntimeConfGetter.class));
    imageBundleUtil = mock(ImageBundleUtil.class);
    task = spy(new CheckOCIImageEligibility(baseTaskDependencies, imageBundleUtil));
  }

  @Test
  public void testNotMarketplaceBasedImagePasses() {
    run(bundleWithStoredFlag(false));
    verifyNoInteractions(imageBundleUtil);
  }

  @Test
  public void testMarketplaceBasedImageFails() {
    BundleImage marketplaceBased = bundleWithStoredFlag(true);

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> run(bundleWithStoredFlag(false), marketplaceBased));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(
        e.getMessage(),
        e.getMessage().contains(imageId(marketplaceBased) + " in region " + REGION));
    assertTrue(e.getMessage(), e.getMessage().contains("editing the universe"));
    assertTrue(e.getMessage(), e.getMessage().contains(ALLOW_MARKETPLACE_BASED_KEY));
  }

  @Test
  public void testMissingFlagIsLookedUpAndStored() {
    BundleImage notMarketplaceBased = bundleWithStoredFlag(null);
    BundleImage marketplaceBased = bundleWithStoredFlag(null);
    when(imageBundleUtil.lookUpImageMarketplaceBased(
            any(Provider.class), eq(REGION), eq(imageId(notMarketplaceBased))))
        .thenReturn(false);
    when(imageBundleUtil.lookUpImageMarketplaceBased(
            any(Provider.class), eq(REGION), eq(imageId(marketplaceBased))))
        .thenReturn(true);

    run(notMarketplaceBased);
    assertEquals(false, storedFlag(notMarketplaceBased));

    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> run(marketplaceBased));
    assertTrue(e.getMessage(), e.getMessage().contains("is an OCI Marketplace image"));
    assertEquals(true, storedFlag(marketplaceBased));
  }

  @Test
  public void testUndeterminedImageFails() {
    BundleImage lookupThrows = bundleWithStoredFlag(null);
    BundleImage originUnknown = bundleWithStoredFlag(null);
    when(imageBundleUtil.lookUpImageMarketplaceBased(
            any(Provider.class), eq(REGION), eq(imageId(lookupThrows))))
        .thenThrow(new PlatformServiceException(BAD_REQUEST, "lookup failed"));
    when(imageBundleUtil.lookUpImageMarketplaceBased(
            any(Provider.class), eq(REGION), eq(imageId(originUnknown))))
        .thenReturn(null);
    BundleImage otherRegion =
        new BundleImage(bundleWithStoredFlag(false).getImageBundleUuid(), "us-phoenix-1");

    for (BundleImage target : List.of(lookupThrows, originUnknown, otherRegion)) {
      PlatformServiceException e = assertThrows(PlatformServiceException.class, () -> run(target));
      assertEquals(BAD_REQUEST, e.getHttpStatus());
      assertTrue(e.getMessage(), e.getMessage().contains("YBA could not determine"));
      assertTrue(e.getMessage(), e.getMessage().contains(ALLOW_MARKETPLACE_BASED_KEY));
    }
    assertNull(storedFlag(lookupThrows));
    assertNull(storedFlag(originUnknown));
  }

  @Test
  public void testCurrentMarketplaceBasedImageFails() {
    BundleImage current = bundleWithStoredFlag(true);

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> run(List.of(bundleWithStoredFlag(false)), List.of(current)));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(
        e.getMessage(),
        e.getMessage().startsWith("Current image " + imageId(current) + " in region "));
  }

  @Test
  public void testAllImagesPassWhenAllowed() {
    mutableConfigFactory.forUniverse(universe).setValue(ALLOW_MARKETPLACE_BASED_KEY, "true");

    run(
        List.of(bundleWithStoredFlag(true), bundleWithStoredFlag(null)),
        List.of(bundleWithStoredFlag(true)));
    verifyNoInteractions(imageBundleUtil);
  }

  private BundleImage bundleWithStoredFlag(Boolean isImageMarketplaceBased) {
    String imageId = "ocid1.image.oc1.iad.image" + ++bundleCount;
    BundleInfo info = new BundleInfo();
    info.setYbImage(imageId);
    info.setIsImageMarketplaceBased(isImageMarketplaceBased);
    ImageBundleDetails details = new ImageBundleDetails();
    details.setRegions(new HashMap<>(Map.of(REGION, info)));
    ImageBundle bundle = ImageBundle.create(provider, "bundle" + bundleCount, details, false);
    return new BundleImage(bundle.getUuid(), REGION);
  }

  private static BundleInfo regionInfo(BundleImage image) {
    return ImageBundle.get(image.getImageBundleUuid()).getDetails().getRegions().get(REGION);
  }

  private static String imageId(BundleImage image) {
    return regionInfo(image).getYbImage();
  }

  private static Boolean storedFlag(BundleImage image) {
    return regionInfo(image).getIsImageMarketplaceBased();
  }

  private void run(BundleImage... targets) {
    run(List.of(targets), List.of());
  }

  private void run(List<BundleImage> targets, List<BundleImage> currents) {
    CheckOCIImageEligibility.Params params = new CheckOCIImageEligibility.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.targetImages.addAll(targets);
    params.currentImages.addAll(currents);
    doReturn(params).when(task).taskParams();
    task.run();
  }
}
