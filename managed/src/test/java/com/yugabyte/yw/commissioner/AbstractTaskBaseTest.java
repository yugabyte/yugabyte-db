// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import java.util.List;
import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

public class AbstractTaskBaseTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer defaultCustomer;
  private Universe universe;

  private AbstractTaskBaseFake task;

  @InjectMocks private AlertService alertService;

  @Mock private BaseTaskDependencies baseTaskDependencies;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    when(baseTaskDependencies.getAlertService()).thenReturn(alertService);

    task = new AbstractTaskBaseFake();
    task.setUserTaskUUID(UUID.randomUUID());

    CustomerTask.create(
        defaultCustomer,
        universe.universeUUID,
        task.userTaskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Update,
        "Test Universe");
  }

  @Test
  public void testSendNotification() {
    AlertFilter filter = AlertFilter.builder().customerUuid(defaultCustomer.getUuid()).build();
    assertEquals(0, alertService.list(filter).size());

    task.sendNotification();

    List<Alert> alerts = alertService.list(filter);
    assertEquals(1, alerts.size());

    Alert alert = alerts.get(0);
    assertEquals(defaultCustomer.uuid, alert.getCustomerUUID());
    assertEquals(
        universe.universeUUID.toString(), alert.getLabelValue(KnownAlertLabels.TARGET_UUID));
    assertEquals("universe", alert.getLabelValue(KnownAlertLabels.TARGET_TYPE));
    assertEquals(KnownAlertCodes.TASK_FAILURE.name(), alert.getErrCode());
    assertEquals(KnownAlertTypes.Error.name(), alert.getType());
  }

  private class AbstractTaskBaseFake extends AbstractTaskBase {
    private AbstractTaskBaseFake() {
      super(baseTaskDependencies);
    }

    @Override
    public void run() {
      // Nothing to do.
    }
  }
}
