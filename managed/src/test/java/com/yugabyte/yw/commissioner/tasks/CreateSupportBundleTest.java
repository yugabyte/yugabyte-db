// Copyright (c) YugaByte, Inc.

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

import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.Date;
import java.util.EnumSet;
import java.util.List;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CreateSupportBundleTest extends CommissionerBaseTest {
  private Universe universe;
  private Customer customer;
  protected RuntimeConfigFactory runtimeConfigFactory;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.runtimeConfigFactory = mockBaseTaskDependencies.getRuntimeConfigFactory();
  }

  @After
  public void tearDown() throws IOException {
    // Delete all the fake support bundles created
    String tmpStoragePath =
        runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    File tmpDirectory = new File(tmpStoragePath);
    for (File f : tmpDirectory.listFiles()) {
      if (f.getName().startsWith("yb-support-bundle-")) {
        f.delete();
      }
    }
  }

  private TaskInfo submitTask(Date startDate, Date endDate) {
    // Filling all the bundle details with fake data
    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.startDate = startDate;
    bundleData.endDate = endDate;
    bundleData.components = EnumSet.allOf(ComponentType.class);
    SupportBundle supportBundle = SupportBundle.create(bundleData, universe);
    SupportBundleTaskParams bundleTaskParams =
        new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundle, bundleTaskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testCreateSupportBundleWithStartAndEndDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(new Date(), new Date());
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundleWithOnlyStartDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(new Date(), null);
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundleWithOnlyEndDate() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(null, new Date());
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }

  @Test
  public void testCreateSupportBundleWithNoDates() throws Exception {
    when(mockSupportBundleComponentFactory.getComponent(any()))
        .thenReturn(mockSupportBundleComponent);
    doNothing()
        .when(mockSupportBundleComponent)
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if task succeeded
    TaskInfo taskInfo = submitTask(null, null);
    assertEquals(Success, taskInfo.getTaskState());

    // Check if the components are executed
    verify(mockSupportBundleComponent, atLeast(1))
        .downloadComponentBetweenDates(any(), any(), any(), any(), any(), any());

    // Check if bundle exists in db
    List<SupportBundle> supportBundleList = SupportBundle.getAll();
    assertEquals(supportBundleList.size(), 1);

    // Check if bundle path exists
    File bundleFile = new File(supportBundleList.get(0).getPath());
    assertTrue(bundleFile.isFile());
  }
}
