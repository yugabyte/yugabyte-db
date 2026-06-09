// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertThrows;

import api.v2.handlers.ImageBundleManagementHandler;
import api.v2.models.ImageBundleApiFilter;
import api.v2.models.ImageBundlePagedQuerySpec;
import api.v2.models.ImageBundlePagedResp;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.ImageBundleDetails;
import com.yugabyte.yw.models.Provider;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

/*
 * Handler unit tests use in-memory H2 which does not support the Postgres JSON filter used when arch is set.
 * Arch filtering is therefore exercised only where the DB matches production (e.g. integration tests), not here.
 */
public class ImageBundleManagementHandlerTest extends FakeDBApplication {

  private ImageBundleManagementHandler handler;
  private Customer customer;
  private Provider provider;

  ImageBundlePagedQuerySpec getSpec() {
    ImageBundlePagedQuerySpec spec = new ImageBundlePagedQuerySpec();
    spec.offset(0).limit(10);
    return spec;
  }

  @Before
  public void setUp() {
    handler = app.injector().instanceOf(ImageBundleManagementHandler.class);
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.newProvider(customer, CloudType.aws);

    ImageBundleDetails details = new ImageBundleDetails();
    Map<String, ImageBundleDetails.BundleInfo> regionImageInfo = new HashMap<>();
    ImageBundleDetails.BundleInfo bundleInfo = new ImageBundleDetails.BundleInfo();
    bundleInfo.setYbImage("yb_image");
    regionImageInfo.put("us-west-1", bundleInfo);
    details.setRegions(regionImageInfo);
    details.setArch(Architecture.x86_64);
    ImageBundle.create(provider, "bundle-a", details, true);

    ImageBundleDetails detailsArm = new ImageBundleDetails();
    detailsArm.setRegions(new HashMap<>(regionImageInfo));
    detailsArm.setArch(Architecture.aarch64);
    ImageBundle.create(provider, "bundle-b", detailsArm, false);
  }

  @Test
  public void pageListImageBundles_returnsBundles() {
    ImageBundlePagedResp resp =
        handler.pageListImageBundles(customer.getUuid(), provider.getUuid(), getSpec());

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(2));
    assertThat(resp.getEntities().size(), is(2));
  }

  @Test
  public void pageListImageBundles_invalidArch() {
    ImageBundlePagedQuerySpec spec =
        getSpec().filter(new ImageBundleApiFilter().arch("not_a_real_arch"));

    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListImageBundles(customer.getUuid(), provider.getUuid(), spec));
  }

  @Test
  public void pageListImageBundles_invalidProvider() {
    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListImageBundles(customer.getUuid(), UUID.randomUUID(), getSpec()));
  }
}
