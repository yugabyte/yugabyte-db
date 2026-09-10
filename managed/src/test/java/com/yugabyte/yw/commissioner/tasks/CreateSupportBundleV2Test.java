// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.Date;
import java.util.EnumSet;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CreateSupportBundleV2Test extends CommissionerBaseTest {
  private Universe universe;
  private Customer customer;
  protected RuntimeConfigFactory runtimeConfigFactory;
  protected RuntimeConfGetter runtimeConfGetter;

  @Before
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.runtimeConfigFactory = mockBaseTaskDependencies.getRuntimeConfigFactory();
    this.runtimeConfGetter = mockBaseTaskDependencies.getConfGetter();
  }

  @After
  public void tearDown() throws IOException {
    String tmpStoragePath =
        runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    File tmpDirectory = new File(tmpStoragePath);
    for (File f : tmpDirectory.listFiles()) {
      if (f.getName().startsWith("yb-support-bundle-")) {
        f.delete();
      }
    }
  }

  @Test
  public void testCreateSupportBundleV2UsesV2Table() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);
    SupportBundleV2 supportBundle =
        SupportBundleV2.createYbaOnly(bundleData, customer, runtimeConfGetter);
    SupportBundleTaskParamsV2 bundleTaskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);

    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, bundleTaskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any(), any());

    SupportBundleV2 bundle = SupportBundleV2.get(supportBundle.getBundleUUID());
    assertTrue(bundle.getPathObject().getFileName().toString().contains("yb-support-bundle-yba-"));
    assertEquals(customer.getUuid(), bundle.getCustomerUUID());
    assertNull(bundle.getScopeUUID());
    assertNull(com.yugabyte.yw.models.SupportBundle.get(supportBundle.getBundleUUID()));
  }
}
