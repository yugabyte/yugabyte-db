// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;

import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.util.Date;
import java.util.EnumSet;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class SupportBundleTaskParamsV2MapperTest extends FakeDBApplication {
  private Customer customer;
  private Universe universe;
  private RuntimeConfGetter confGetter;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
  }

  private SupportBundleTaskParamsV2 v2Params(SupportBundleFormDataV2 bundleData) {
    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, confGetter);
    return new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);
  }

  private SupportBundleFormDataV2 bundleData() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    return bundleData;
  }

  @Test
  public void testToV1TaskParamsCopiesBundleDetails() {
    SupportBundleFormDataV2 bundleData = bundleData();
    SupportBundleTaskParamsV2 v2Params = v2Params(bundleData);

    SupportBundleTaskParams v1Params =
        SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(v2Params);

    assertNotNull(v1Params.supportBundle);
    assertEquals(v2Params.supportBundle.getBundleUUID(), v1Params.supportBundle.getBundleUUID());
    assertNotNull(v1Params.supportBundle.getBundleDetails());
    assertEquals(
        v2Params.supportBundle.getBundleDetails().getComponents(),
        v1Params.supportBundle.getBundleDetails().getComponents());
    assertEquals(customer, v1Params.customer);
    assertEquals(universe, v1Params.universe);
    assertEquals(bundleData.startDate, v1Params.bundleData.startDate);
  }

  @Test
  public void testToV1TaskParamsMapsScopeAndStatus() {
    SupportBundleTaskParamsV2 v2Params = v2Params(bundleData());

    SupportBundleTaskParams v1Params =
        SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(v2Params);

    assertEquals(v2Params.scopeUUID, v1Params.scopeUUID);
    assertEquals(SupportBundleStatusType.Running, v1Params.supportBundle.getStatus());
  }

  /**
   * The collection layer works off the same entities the task already loaded, so these must be the
   * very same instances rather than copies MapStruct synthesized.
   */
  @Test
  public void testToV1TaskParamsPassesEntitiesByReference() {
    SupportBundleTaskParamsV2 v2Params = v2Params(bundleData());

    SupportBundleTaskParams v1Params =
        SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(v2Params);

    assertSame(v2Params.customer, v1Params.customer);
    assertSame(v2Params.universe, v1Params.universe);
  }

  /** Retry bookkeeping belongs to the running task, not to the derived v1 view of its params. */
  @Test
  public void testToV1TaskParamsDropsRetryBookkeeping() {
    SupportBundleTaskParamsV2 v2Params = v2Params(bundleData());
    v2Params.setErrorString("boom");
    v2Params.setPreviousTaskUUID(UUID.randomUUID());

    SupportBundleTaskParams v1Params =
        SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(v2Params);

    assertNull(v1Params.getErrorString());
    assertNull(v1Params.getPreviousTaskUUID());
  }

  /**
   * The v1 path setter dereferences its argument, and the bundle path is still unset when the task
   * bridges its params, so the mapper must leave it alone.
   */
  @Test
  public void testToV1SupportBundleLeavesUnsetPathAlone() {
    SupportBundleTaskParamsV2 v2Params = v2Params(bundleData());

    SupportBundleTaskParams v1Params =
        SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(v2Params);

    assertNull(v1Params.supportBundle.getPath());
  }

  @Test
  public void testToV1StatusMapsEveryV2Constant() {
    for (SupportBundleV2StatusType v2Status : SupportBundleV2StatusType.values()) {
      assertEquals(
          SupportBundleStatusType.valueOf(v2Status.name()),
          SupportBundleTaskParamsV2Mapper.INSTANCE.toV1Status(v2Status));
    }
  }

  @Test
  public void testNullSourcesMapToNull() {
    assertNull(SupportBundleTaskParamsV2Mapper.INSTANCE.toV1TaskParams(null));
    assertNull(SupportBundleTaskParamsV2Mapper.INSTANCE.toV1SupportBundle(null));
    assertNull(SupportBundleTaskParamsV2Mapper.INSTANCE.toV1Status(null));
  }
}
