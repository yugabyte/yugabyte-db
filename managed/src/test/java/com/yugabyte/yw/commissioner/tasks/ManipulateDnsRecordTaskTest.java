// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManipulateDnsRecordTask;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ManipulateDnsRecordTaskTest extends CommissionerBaseTest {

  private TaskInfo submitTask() {
    Customer customer = ModelFactory.testCustomer();
    Users defaultUser = ModelFactory.testUser(customer);
    Universe universe = createUniverse(customer.getId());
    AbstractTaskBase.createTask(ManipulateDnsRecordTask.class);
    ManipulateDnsRecordTask.Params params = new ManipulateDnsRecordTask.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.type = DnsManager.DnsCommandType.Create;
    params.providerUUID = UUID.randomUUID();
    params.hostedZoneId = "";
    params.domainNamePrefix = "";

    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);
    try {
      UUID taskUUID = commissioner.submit(TaskType.ManipulateDnsRecordTask, params);
      CustomerTask.create(
          customer,
          universe.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.Create,
          "Create Universe");
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testSimpleTaskSuccess() {
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message = "";
    when(mockDnsManager.manipulateDnsRecord(any(), any(), anyString(), anyString(), anyString()))
        .thenReturn(response);

    TaskInfo taskInfo = submitTask();
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockDnsManager, times(1))
        .manipulateDnsRecord(any(), any(), anyString(), anyString(), anyString());
  }
}
