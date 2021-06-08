/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import akka.dispatch.Dispatcher;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import scala.concurrent.ExecutionContext;

import java.util.UUID;

import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.Silent.class)
public class AlertConfigurationWriterTest extends FakeDBApplication {

  @Mock private ExecutionContext executionContext;

  @Mock private ActorSystem actorSystem;

  @Mock private SwamperHelper swamperHelper;

  @Mock private MetricQueryHelper queryHelper;

  @Mock private RuntimeConfigFactory configFactory;

  private AlertDefinitionService alertDefinitionService;

  private AlertConfigurationWriter configurationWriter;

  private Customer customer;

  private Universe universe;

  @Mock private Config globalConfig;

  private AlertDefinition definition;

  @Before
  public void setUp() {
    alertDefinitionService = new AlertDefinitionService();
    when(actorSystem.scheduler()).thenReturn(mock(Scheduler.class));
    when(globalConfig.getInt(AlertConfigurationWriter.CONFIG_SYNC_INTERVAL_PARAM)).thenReturn(1);
    when(configFactory.globalRuntimeConf()).thenReturn(globalConfig);
    when(actorSystem.dispatcher()).thenReturn(mock(Dispatcher.class));
    configurationWriter =
        new AlertConfigurationWriter(
            executionContext,
            actorSystem,
            alertDefinitionService,
            swamperHelper,
            queryHelper,
            configFactory);

    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getCustomerId());

    definition = ModelFactory.createAlertDefinition(customer, universe);
  }

  @Test
  public void testSyncActiveDefinition() {
    configurationWriter.syncDefinitions();

    verify(swamperHelper, times(1)).writeAlertDefinition(definition);
    verify(queryHelper, times(1)).postManagementCommand("reload");
  }

  @Test
  public void testSyncNotActiveDefinition() {
    definition.setActive(false);
    definition = alertDefinitionService.update(definition);

    configurationWriter.syncDefinitions();

    verify(swamperHelper, times(1)).removeAlertDefinition(definition.getUuid());
    verify(queryHelper, times(1)).postManagementCommand("reload");
  }

  @Test
  public void testSyncExistingAndMissingDefinitions() {
    UUID missingDefinitionUuid = UUID.randomUUID();
    when(swamperHelper.getAlertDefinitionConfigUuids())
        .thenReturn(ImmutableList.of(missingDefinitionUuid));
    configurationWriter.syncDefinitions();

    verify(swamperHelper, times(1)).writeAlertDefinition(definition);
    verify(swamperHelper, times(1)).removeAlertDefinition(missingDefinitionUuid);
    verify(queryHelper, times(1)).postManagementCommand("reload");
  }

  @Test
  public void testNothingToSync() {
    alertDefinitionService.delete(definition.getUuid());

    configurationWriter.syncDefinitions();

    verify(swamperHelper, never()).writeAlertDefinition(any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Called once after startup
    verify(queryHelper, times(1)).postManagementCommand("reload");

    configurationWriter.syncDefinitions();

    verify(swamperHelper, never()).writeAlertDefinition(any());
    verify(swamperHelper, never()).removeAlertDefinition(any());
    // Not called on subsequent run
    verify(queryHelper, times(1)).postManagementCommand("reload");
  }
}
