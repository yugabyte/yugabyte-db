// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowMinusWithoutMillis;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelManager;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateService;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateServiceTest;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertNotificationContext;
import com.yugabyte.yw.common.alerts.AlertNotificationReport;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertChannelEmail;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.Collections;
import java.util.Date;
import java.util.UUID;
import javax.mail.MessagingException;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertManagerTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static final String DEFAULT_EMAIL = "to@to.com";

  private static final String ALERT_DESTINATION_NAME = "Test AlertDestination";

  private Customer defaultCustomer;

  @Mock private AlertChannelEmail emailChannel;

  @Mock private AlertChannelManager channelsManager;

  @Mock private EmailHelper emailHelper;

  private AlertManager am;

  private AlertConfiguration configuration;

  private AlertDefinition definition;

  private Universe universe;

  private AlertNotificationReport report = new AlertNotificationReport();
  private AlertNotificationContext context =
      AlertNotificationContext.builder().alertingConfigByCustomer(Collections.emptyMap()).build();

  private AlertDestination defaultDestination;
  private AlertChannel defaultChannel;

  private AlertChannelService alertChannelService;
  private AlertChannelTemplateService alertChannelTemplateService;
  private AlertDestinationService alertDestinationService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    when(channelsManager.get(ChannelType.Email.name())).thenReturn(emailChannel);

    universe = ModelFactory.createUniverse();
    configuration = ModelFactory.createAlertConfiguration(defaultCustomer, universe);
    definition = ModelFactory.createAlertDefinition(defaultCustomer, universe, configuration);

    alertChannelService = app.injector().instanceOf(AlertChannelService.class);
    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
    am =
        new AlertManager(
            emailHelper,
            alertService,
            alertConfigurationService,
            alertChannelService,
            alertChannelTemplateService,
            alertDestinationService,
            channelsManager,
            metricService);

    defaultDestination =
        alertDestinationService.createDefaultDestination(defaultCustomer.getUuid());
    defaultChannel = defaultDestination.getChannelsList().get(0);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));
  }

  @Test
  public void testSendNotification_MetricsSetOk() {
    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, defaultCustomer));
    am.setChannelStatusMetric(PlatformMetrics.ALERT_MANAGER_CHANNEL_STATUS, defaultChannel, false);

    Alert alert = ModelFactory.createAlert(defaultCustomer);

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    Metric amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .sourceUuid(defaultCustomer.getUuid())
                .build(),
            1.0);
    Metric channelStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_CHANNEL_STATUS.getMetricName())
                .sourceUuid(defaultChannel.getUuid())
                .build(),
            1.0);
  }

  @Test
  public void testSendNotification_FailureMetric() throws PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer);

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    doThrow(new PlatformNotificationException("test"))
        .when(emailChannel)
        .sendNotification(eq(defaultCustomer), captor.capture(), any(), any());
    am.sendNotificationForState(alert, State.ACTIVE, report, context);
    assertThat(captor.getValue().getUuid(), equalTo(alert.getUuid()));

    Metric channelStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_CHANNEL_STATUS.getMetricName())
                .sourceUuid(defaultChannel.getUuid())
                .build(),
            0.0);
  }

  @Test
  public void testSendNotification_NoDestinations() throws MessagingException {
    configuration.setDefaultDestination(false);
    alertConfigurationService.save(configuration);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    assertThat(alert.getNotificationsFailed(), equalTo(0));
    assertThat(alert.getNextNotificationTime(), nullValue());
  }

  @Test
  public void testSendNotification_NoConfig() throws MessagingException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    alert.setLabel(KnownAlertLabels.CONFIGURATION_UUID, UUID.randomUUID().toString());

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    assertThat(alert.getNotificationsFailed(), equalTo(0));
    assertThat(alert.getNextNotificationTime(), nullValue());
  }

  @Test
  public void testSendNotification_DefaultDestinationMissing() throws MessagingException {
    defaultDestination.setDefaultDestination(false);
    defaultDestination.save();
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    assertThat(alert.getNotificationsFailed(), equalTo(1));
    assertThat(alert.getNextNotificationTime().after(new Date()), equalTo(true));
  }

  @Test
  public void testSendNotification_TwoEmailDestinations()
      throws MessagingException, PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertChannel channel1 =
        ModelFactory.createEmailChannel(defaultCustomer.getUuid(), "AlertChannel 1");
    AlertChannel channel2 =
        ModelFactory.createEmailChannel(defaultCustomer.getUuid(), "AlertChannel 2");
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            defaultCustomer.getUuid(),
            ALERT_DESTINATION_NAME,
            ImmutableList.of(channel1, channel2));
    configuration.setDestinationUUID(destination.getUuid());
    configuration.save();

    AlertChannelTemplates templates =
        AlertChannelTemplateServiceTest.createTemplates(
            defaultCustomer.getUuid(), ChannelType.Email);
    alertChannelTemplateService.save(templates);
    AlertChannelTemplatesExt templatesWithDefaults =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);

    am.sendNotificationForState(alert, State.ACTIVE, report, context);
    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    verify(emailChannel, times(2)).sendNotification(any(), any(), any(), eq(templatesWithDefaults));
  }

  @Test
  public void testDefaultDestination_IsUsed() throws PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);

    am.sendNotificationForState(alert, State.ACTIVE, report, context);
    ArgumentCaptor<AlertChannel> channelCaptor = ArgumentCaptor.forClass(AlertChannel.class);
    verify(emailChannel, times(1)).sendNotification(any(), any(), channelCaptor.capture(), any());

    assertThat(channelCaptor.getValue().getParams().getChannelType(), equalTo(ChannelType.Email));
    AlertChannelEmailParams params = (AlertChannelEmailParams) channelCaptor.getValue().getParams();
    assertThat(params.getRecipients(), nullValue());
    assertThat(params.isDefaultRecipients(), is(true));
  }

  @Test
  public void testDefaultDestination_EmptyRecipientsAlertResolved()
      throws PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.emptyList());

    am.sendNotificationForState(alert, State.ACTIVE, report, context);
    verify(emailChannel, never()).sendNotification(any(), any(), any(), any());

    Metric amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .sourceUuid(defaultCustomer.getUuid())
                .build(),
            0.0);

    // Restoring recipients.
    when(emailHelper.getDestinations(defaultCustomer.getUuid()))
        .thenReturn(Collections.singletonList(DEFAULT_EMAIL));

    am.sendNotificationForState(alert, State.ACTIVE, report, context);
    verify(emailChannel, times(1)).sendNotification(any(), any(), any(), any());

    amStatus =
        AssertHelper.assertMetricValue(
            metricService,
            MetricKey.builder()
                .customerUuid(defaultCustomer.getUuid())
                .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
                .sourceUuid(defaultCustomer.getUuid())
                .build(),
            1.0);
  }

  // Aren't checking ACKNOWLEDGED in any state fields as such alert should not be
  // scheduled.
  @Parameters({
    // @formatter:off
    "null, ACTIVE, 1",
    "null, RESOLVED, 2",
    "ACTIVE, RESOLVED, 1",
    // @formatter:on
  })
  @TestCaseName(
      "{method}(Last sent state:{0}, current state:{1}, " + "expected notifications count:{2})")
  @Test
  public void testSendNotifications_CountMatched(
      @Nullable State notifiedState, State currentState, int expectedCount)
      throws PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert
        .setState(currentState)
        .setDefinitionUuid(definition.getUuid())
        .setNotifiedState(notifiedState);
    if (expectedCount > 0) {
      alert.setNextNotificationTime(Date.from(new Date().toInstant().minusSeconds(10)));
    }
    alert.save();

    am.sendNotifications();

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    verify(emailChannel, times(expectedCount))
        .sendNotification(eq(defaultCustomer), captor.capture(), any(), any());

    if (expectedCount > 0) {
      assertThat(captor.getValue().getUuid(), equalTo(alert.getUuid()));

      Alert updatedAlert = alertService.get(alert.getUuid());
      assertThat(updatedAlert.getNextNotificationTime(), nullValue());
      assertThat(updatedAlert.getNotificationAttemptTime(), notNullValue());
      assertThat(updatedAlert.getNotificationsFailed(), is(0));
    }
  }

  @Test
  public void testSendNotificationForState_WithAnotherState() throws PlatformNotificationException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    alert.setState(State.RESOLVED);
    alert.save();

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    ArgumentCaptor<Alert> captor = ArgumentCaptor.forClass(Alert.class);
    verify(emailChannel, times(1))
        .sendNotification(eq(defaultCustomer), captor.capture(), any(), any());
    assertThat(captor.getValue().getState(), is(State.ACTIVE));
  }

  @Test
  public void testSendNotificationForState_() throws MessagingException {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    defaultDestination.setDefaultDestination(false);
    defaultDestination.save();

    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    assertThat(alert.getNotificationsFailed(), equalTo(1));
    assertThat(alert.getNextNotificationTime().after(new Date()), equalTo(true));

    alert = ModelFactory.createAlert(defaultCustomer, definition);
    alert.setCreateTime(nowMinusWithoutMillis(2, ChronoUnit.DAYS));
    am.sendNotificationForState(alert, State.ACTIVE, report, context);

    verify(emailHelper, never()).sendEmail(any(), anyString(), anyString(), any(), any());
    assertThat(alert.getNotificationsFailed(), equalTo(1));
    assertThat(alert.getNextNotificationTime(), nullValue());
  }

  @Test
  public void testActiveAlertNotificationInterval() {
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertChannel channel =
        ModelFactory.createEmailChannel(defaultCustomer.getUuid(), "AlertChannel 1");
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            defaultCustomer.getUuid(), ALERT_DESTINATION_NAME, ImmutableList.of(channel));
    configuration.setDestinationUUID(destination.getUuid());
    configuration.save();

    AlertingData alertingData = new AlertingData();
    alertingData.activeAlertNotificationIntervalMs = 5000;
    AlertNotificationContext contextWithNotificationPeriod =
        AlertNotificationContext.builder()
            .alertingConfigByCustomer(ImmutableMap.of(defaultCustomer.getUuid(), alertingData))
            .build();

    am.sendNotificationForState(alert, State.ACTIVE, report, contextWithNotificationPeriod);

    Alert updatedAlert = alertService.get(alert.getUuid());
    assertThat(updatedAlert.getNotifiedState(), equalTo(State.ACTIVE));
    assertTrue(updatedAlert.getNextNotificationTime().after(new Date()));

    am.sendNotificationForState(alert, State.RESOLVED, report, contextWithNotificationPeriod);

    updatedAlert = alertService.get(alert.getUuid());
    assertThat(updatedAlert.getNotifiedState(), equalTo(State.RESOLVED));
    assertThat(updatedAlert.getNextNotificationTime(), nullValue());
  }
}
