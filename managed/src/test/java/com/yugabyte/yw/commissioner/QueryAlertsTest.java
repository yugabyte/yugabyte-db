// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.metrics.MetricQueryResponse.Entry;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.Alert.TargetType;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import scala.concurrent.ExecutionContext;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

@RunWith(JUnitParamsRunner.class)
public class QueryAlertsTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock private ExecutionContext executionContext;

  @Mock private ActorSystem actorSystem;

  @Mock private AlertManager alertManager;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  private QueryAlerts queryAlerts;

  private Customer customer;

  private Universe universe;

  @Mock private Config universeConfig;

  private AlertDefinition definition;

  @Before
  public void setUp() {
    AlertDefinitionService alertDefinitionService = new AlertDefinitionService();
    when(actorSystem.scheduler()).thenReturn(mock(Scheduler.class));
    queryAlerts =
        new QueryAlerts(
            executionContext,
            actorSystem,
            alertManager,
            queryHelper,
            alertDefinitionService,
            configFactory);

    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    when(configFactory.forUniverse(universe)).thenReturn(universeConfig);

    definition = ModelFactory.createAlertDefinition(customer, universe);
  }

  @Test
  public void testProcessAlertDefinitions_ReplacesParameterInQueryAndCreatesAlert() {
    ArrayList<Entry> queryHelperResult = new ArrayList<>();
    queryHelperResult.add(mock(MetricQueryResponse.Entry.class));
    when(queryHelper.queryDirect("query < 1")).thenReturn(queryHelperResult);
    assertEquals(0, Alert.list(customer.uuid).size());
    queryAlerts.processAlertDefinitions(customer.uuid);
    assertEquals(1, Alert.list(customer.uuid).size());
  }

  @Test
  public void testProcessAlertDefinitions_ReturnsEmptyResult() {
    when(queryHelper.queryDirect("query < 1")).thenReturn(new ArrayList<>());

    assertEquals(0, Alert.list(customer.uuid).size());
    Set<Alert> result = queryAlerts.processAlertDefinitions(customer.uuid);
    assertTrue(result.isEmpty());
    assertEquals(0, Alert.list(customer.uuid).size());
  }

  @Test
  // @formatter:off
  @Parameters({"ACTIVE, 1, true", "CREATED, 1, true", "RESOLVED, 0, false"})
  // @formatter:on
  public void testProcessAlertDefinitions(
      State alertState, int activeAlertsCount, boolean alertReused) {
    ArrayList<Entry> queryHelperResult = new ArrayList<>();
    queryHelperResult.add(mock(MetricQueryResponse.Entry.class));
    when(queryHelper.queryDirect("query < 1")).thenReturn(queryHelperResult);
    Alert alert =
        Alert.create(
            customer.uuid,
            universe.universeUUID,
            TargetType.UniverseType,
            "TEST_CHECK",
            "Warning",
            "Message",
            false,
            definition.getUuid(),
            Collections.emptyList());
    alert.setState(alertState);
    alert.save();

    assertEquals(1, Alert.list(customer.uuid).size());

    List<Alert> activeAlerts = Alert.getActiveCustomerAlerts(customer.uuid, definition.getUuid());
    assertEquals(activeAlertsCount, activeAlerts.size());
    assertEquals(alertReused, activeAlerts.contains(alert));

    Set<Alert> result = queryAlerts.processAlertDefinitions(customer.uuid);
    assertEquals(activeAlertsCount, result.size());
    assertEquals(alertReused, result.contains(alert));

    // If a new alert has been created, we will receive it on the next call to
    // processAlertDefinitions.
    result = queryAlerts.processAlertDefinitions(customer.uuid);
    assertEquals(1, result.size()); /* Always only one active alert for all current scenarios. */
    assertEquals(alertReused, result.contains(alert));

    assertEquals(1 /* initial */ + (alertReused ? 0 : 1), Alert.list(customer.uuid).size());
  }
}
