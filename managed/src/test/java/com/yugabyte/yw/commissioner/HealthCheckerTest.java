// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.CustomerRegisterFormData.AlertingData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Provider;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.api.Play;
import play.Configuration;
import play.Environment;
import play.libs.Json;

import scala.concurrent.ExecutionContext;

import java.util.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class HealthCheckerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(HealthCheckerTest.class);

  private static final String YB_ALERT_TEST_EMAIL = "test@yugabyte.com";

  HealthChecker healthChecker;

  @Mock
  ActorSystem mockActorSystem;
  @Mock
  play.Configuration mockConfig;
  @Mock
  Environment mockEnvironment;
  @Mock
  ExecutionContext mockExecutionContext;
  @Mock
  HealthManager mockHealthManager;
  @Mock
  Scheduler mockScheduler;

  Customer defaultCustomer;
  Provider defaultProvider;

  Universe universe;
  AccessKey accessKey;
  CustomerConfig customerConfig;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);

    when(mockActorSystem.scheduler()).thenReturn(mockScheduler);

    when(mockConfig.getString("yb.health.default_email")).thenReturn(YB_ALERT_TEST_EMAIL);
    ShellProcessHandler.ShellResponse dummyShellResponse =
      ShellProcessHandler.ShellResponse.create(0, "{\"error\": false}");

    when(mockHealthManager.runCommand(
        any(), any(), any(), any(), any(), any(), any())
    ).thenReturn(dummyShellResponse);

    // Finally setup the mocked instance.
    healthChecker = new HealthChecker(
        mockActorSystem,
        mockConfig,
        mockEnvironment,
        mockExecutionContext,
        mockHealthManager);
  }

  private Universe setupUniverse(String name) {
    accessKey = AccessKey.create(
        defaultProvider.uuid,
        "key-" + name,
        new AccessKey.KeyInfo());

    universe = ModelFactory.createUniverse(name, defaultCustomer.getCustomerId());
    // Universe modifies customer, so we need to refresh our in-memory view of this reference.
    defaultCustomer = Customer.get(defaultCustomer.uuid);

    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    userIntent.accessKeyCode = accessKey.getKeyCode();
    Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent));
    return Universe.get(universe.universeUUID);
  }

  private void setupAlertingData(String alertingEmail, boolean sendAlertsToYb) {
    // Setup alerting data.
    AlertingData data = new AlertingData();
    data.sendAlertsToYb = sendAlertsToYb;
    data.alertingEmail = alertingEmail;
    customerConfig = CustomerConfig.createAlertConfig(defaultCustomer.uuid, Json.toJson(data));
  }

  private void verifyHealthManager(Universe u, String expectedEmail) {
    verify(mockHealthManager, times(1)).runCommand(
        eq(defaultProvider),
        any(),
        eq(u.name),
        eq(String.format("[%s][%s]", defaultCustomer.email, defaultCustomer.code)),
        eq(expectedEmail),
        eq(0L),
        eq(true));
  }

  private void testSingleUniverse(Universe u, String expectedEmail) {
    healthChecker.checkSingleUniverse(u, defaultCustomer, customerConfig, true);
    verifyHealthManager(u, expectedEmail);
  }

  private void validateNoDevopsCall() {
    healthChecker.checkCustomer(defaultCustomer);

    verify(mockHealthManager, times(0)).runCommand(
        any(), any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testSingleUniverseNoEmail() {
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, false);
    testSingleUniverse(u, null);
  }

  @Test
  public void testSingleUniverseYbEmail() {
    Universe u = setupUniverse("univ1");
    setupAlertingData(null, true);
    testSingleUniverse(u, YB_ALERT_TEST_EMAIL);
  }

  @Test
  public void testSingleUniverseCustomEmail() {
    Universe u = setupUniverse("univ1");
    String email = "foo@yugabyte.com";
    setupAlertingData(email, false);
    testSingleUniverse(u, email);
  }

  @Test
  public void testSingleUniverseMultipleEmails() {
    Universe u = setupUniverse("univ1");
    String email = "foo@yugabyte.com";
    setupAlertingData(email, true);
    testSingleUniverse(u, String.format("%s,%s", YB_ALERT_TEST_EMAIL, email));
  }

  @Test
  public void testMultipleUniversesIndividually() {
    Universe univ1 = setupUniverse("univ1");
    Universe univ2 = setupUniverse("univ2");
    setupAlertingData(null, false);
    testSingleUniverse(univ1, null);
    testSingleUniverse(univ2, null);
  }

  @Test
  public void testMultipleUniversesTogether() {
    Universe univ1 = setupUniverse("univ1");
    Universe univ2 = setupUniverse("univ2");
    setupAlertingData(null, false);
    healthChecker.checkAllUniverses(defaultCustomer, customerConfig, true);
    verifyHealthManager(univ1, null);
    verifyHealthManager(univ2, null);
  }

  @Test
  public void testNoUniverse() {
    validateNoDevopsCall();
  }

  @Test
  public void testNoAlertingConfig() {
    Universe u = setupUniverse("univ1");
    validateNoDevopsCall();
  }

  @Test
  public void testInvalidUniverseNullDetails() {
    Universe u = setupUniverse("test");
    // Set the details to null.
    Universe.saveDetails(u.universeUUID, new Universe.UniverseUpdater() {
      @Override
      public void run(Universe univ) {
        univ.setUniverseDetails(null);
      };
    });
    setupAlertingData(null, false);
    // Add a reference to this on the customer anyway.
    validateNoDevopsCall();
  }

  @Test
  public void testInvalidUniverseUpdateInProgress() {
    Universe u = setupUniverse("test");
    // Set updateInProgress to true.
    Universe.saveDetails(u.universeUUID, new Universe.UniverseUpdater() {
      @Override
      public void run(Universe univ) {
        UniverseDefinitionTaskParams details = univ.getUniverseDetails();
        details.updateInProgress = true;
        univ.setUniverseDetails(details);
      };
    });
    setupAlertingData(null, false);
    validateNoDevopsCall();
  }

  @Test
  public void testInvalidUniverseBadProvider() {
    Universe u = setupUniverse("test");
    // Setup an invalid provider.
    Universe.saveDetails(u.universeUUID, new Universe.UniverseUpdater() {
      @Override
      public void run(Universe univ) {
        UniverseDefinitionTaskParams details = univ.getUniverseDetails();
        UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
        userIntent.provider = UUID.randomUUID().toString();
        univ.setUniverseDetails(details);
      };
    });
    setupAlertingData(null, false);
    validateNoDevopsCall();
  }

  @Test
  public void testInvalidUniverseNoAccessKey() {
    Universe u = setupUniverse("test");
    setupAlertingData(null, false);
    accessKey.delete();
    validateNoDevopsCall();
  }

  @Test
  public void testTimingLogic() {
    // Setup some waits.
    long waitMs = 500;
    // Wait one cycle between checks.
    when(mockConfig.getLong("yb.health.check_interval_ms")).thenReturn(waitMs);
    // Wait two cycles between status updates.
    when(mockConfig.getLong("yb.health.status_interval_ms")).thenReturn(2 * waitMs);
    // Default prep.
    Universe u = setupUniverse("test");
    setupAlertingData(null, false);
    // First time we both check and send update.
    healthChecker.checkCustomer(defaultCustomer);
    verify(mockHealthManager, times(1)).runCommand(
        any(), any(), any(), any(), any(), any(), eq(true));
    // If we run right afterwards, none of the timers should be hit again, so total hit with any
    // args should still be 1.
    healthChecker.checkCustomer(defaultCustomer);
    verify(mockHealthManager, times(1)).runCommand(
        any(), any(), any(), any(), any(), any(), any());
    try {
      Thread.sleep(waitMs);
    } catch (InterruptedException e) {
    }
    // One cycle later, we should be running another test, but no status update, so first time
    // running with false.
    healthChecker.checkCustomer(defaultCustomer);
    verify(mockHealthManager, times(1)).runCommand(
        any(), any(), any(), any(), any(), any(), eq(false));
    // Another cycle later, we should be running yet another test, but now with status update.
    try {
      Thread.sleep(waitMs);
    } catch (InterruptedException e) {
    }
    // One cycle later, we should be running another test, but no status update, so second time
    // running with true.
    healthChecker.checkCustomer(defaultCustomer);
    verify(mockHealthManager, times(2)).runCommand(
        any(), any(), any(), any(), any(), any(), eq(true));
  }
}
