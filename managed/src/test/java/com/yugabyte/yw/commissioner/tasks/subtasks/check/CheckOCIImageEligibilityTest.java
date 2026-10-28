// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.oracle.bmc.core.model.Image;
import com.yugabyte.yw.cloud.oci.OCICloudImpl;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.subtasks.check.CheckOCIImageEligibility.TargetImage;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Provider;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class CheckOCIImageEligibilityTest extends FakeDBApplication {

  private static final String REGION = "us-ashburn-1";
  private static final String CUSTOM_IMAGE = "ocid1.image.oc1.iad.custom";
  private static final String MARKETPLACE_IMAGE = "ocid1.image.oc1.iad.marketplace";

  private OCICloudImpl mockOCICloudImpl;
  private CheckOCIImageEligibility task;
  private Provider provider;

  @Before
  public void setUp() {
    provider = ModelFactory.ociProvider(ModelFactory.testCustomer());
    mockOCICloudImpl = mock(OCICloudImpl.class);
    task = spy(new CheckOCIImageEligibility(mock(BaseTaskDependencies.class), mockOCICloudImpl));
    stubImage(CUSTOM_IMAGE, "ocid1.compartment.oc1..custom");
  }

  @Test
  public void testCustomImagePasses() {
    run(CUSTOM_IMAGE);
  }

  @Test
  public void testMarketplaceImageFails() {
    stubImage(MARKETPLACE_IMAGE, "publisherCompartment");

    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> run(CUSTOM_IMAGE, MARKETPLACE_IMAGE));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(e.getMessage(), e.getMessage().contains(MARKETPLACE_IMAGE + " in region " + REGION));
  }

  private void stubImage(String imageId, String compartmentId) {
    Image image = Image.builder().id(imageId).compartmentId(compartmentId).build();
    when(mockOCICloudImpl.getImageOrBadRequest(any(Provider.class), eq(REGION), eq(imageId)))
        .thenReturn(image);
  }

  private void run(String... imageIds) {
    CheckOCIImageEligibility.Params params = new CheckOCIImageEligibility.Params();
    params.setUniverseUUID(UUID.randomUUID());
    for (String imageId : imageIds) {
      params.targetImages.add(new TargetImage(provider.getUuid(), REGION, imageId));
    }
    doReturn(params).when(task).taskParams();
    task.run();
  }
}
