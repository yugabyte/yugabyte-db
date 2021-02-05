// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import java.util.ArrayList;
import java.util.Set;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.metrics.MetricQueryResponse.Entry;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.Alert.TargetType;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import scala.concurrent.ExecutionContext;

@RunWith(MockitoJUnitRunner.class)
public class QueryAlertsTest extends FakeDBApplication {

  @Mock
  private ExecutionContext executionContext;

  @Mock
  private ActorSystem actorSystem;

  @Mock
  private AlertManager alertManager;

  @Mock
  private MetricQueryHelper queryHelper;

  @Mock
  private RuntimeConfigFactory configFactory;

  private QueryAlerts queryAlerts;

  private Customer customer;

  private Universe universe;

  @Mock
  private Config universeConfig;

  private AlertDefinition definition;

  @Before
  public void setUp() {
    when(actorSystem.scheduler()).thenReturn(mock(Scheduler.class));
    queryAlerts = new QueryAlerts(executionContext, actorSystem, alertManager, queryHelper,
        configFactory);

    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getCustomerId());
    when(configFactory.forUniverse(universe)).thenReturn(universeConfig);

    definition = AlertDefinition.create(customer.uuid, universe.universeUUID, "alertDefinition",
        "query {{ test.parameter }}", true);
  }

  @Test
  public void testProcessAlertDefinitions_ReplacesParameterInQueryAndCreatesAlert() {
    ArrayList<Entry> queryHelperResult = new ArrayList<>();
    queryHelperResult.add(mock(MetricQueryResponse.Entry.class));
    when(queryHelper.queryDirect("query test")).thenReturn(queryHelperResult);
    when(universeConfig.getString("test.parameter")).thenReturn("test");

    assertEquals(0, Alert.list(customer.uuid).size());
    queryAlerts.processAlertDefinitions(customer.uuid);
    assertEquals(1, Alert.list(customer.uuid).size());
  }

  @Test
  public void testProcessAlertDefinitions_ReturnsEmptyResult() {
    when(queryHelper.queryDirect("query test")).thenReturn(new ArrayList<>());
    when(universeConfig.getString("test.parameter")).thenReturn("test");

    assertEquals(0, Alert.list(customer.uuid).size());
    Set<Alert> result = queryAlerts.processAlertDefinitions(customer.uuid);
    assertTrue(result.isEmpty());
    assertEquals(0, Alert.list(customer.uuid).size());
  }

  @Test
  public void testProcessAlertDefinitions_ReturnsAlreadyExistingAlert() {
    ArrayList<Entry> queryHelperResult = new ArrayList<>();
    queryHelperResult.add(mock(MetricQueryResponse.Entry.class));
    when(queryHelper.queryDirect("query test")).thenReturn(queryHelperResult);
    when(universeConfig.getString("test.parameter")).thenReturn("test");

    Alert alert = Alert.create(customer.uuid, universe.universeUUID, TargetType.UniverseType,
        "TEST_CHECK", "Warning", "Message", false, definition.uuid);
    alert.setState(State.ACTIVE);
    alert.save();

    assertEquals(1, Alert.list(customer.uuid).size());
    assertEquals(alert, Alert.getActiveCustomerAlert(customer.uuid, definition.uuid));

    Set<Alert> result = queryAlerts.processAlertDefinitions(customer.uuid);

    // Returns the same alert.
    assertEquals(1, result.size());
    assertTrue(result.contains(alert));
    // Doesn't create another alert.
    assertEquals(1, Alert.list(customer.uuid).size());
  }

}
