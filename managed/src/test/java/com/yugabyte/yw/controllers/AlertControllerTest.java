// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AlertTemplate.ALERT_CONFIG_WRITING_FAILED;
import static com.yugabyte.yw.common.AlertTemplate.ALERT_NOTIFICATION_CHANNEL_ERROR;
import static com.yugabyte.yw.common.AlertTemplate.ALERT_NOTIFICATION_ERROR;
import static com.yugabyte.yw.common.AlertTemplate.ALERT_QUERY_FAILED;
import static com.yugabyte.yw.common.AlertTemplate.BACKUP_FAILURE;
import static com.yugabyte.yw.common.AlertTemplate.BACKUP_SCHEDULE_FAILURE;
import static com.yugabyte.yw.common.AlertTemplate.CLIENT_TO_NODE_CA_CERT_EXPIRY;
import static com.yugabyte.yw.common.AlertTemplate.CLIENT_TO_NODE_CERT_EXPIRY;
import static com.yugabyte.yw.common.AlertTemplate.CLOCK_SKEW;
import static com.yugabyte.yw.common.AlertTemplate.DB_COMPACTION_OVERLOAD;
import static com.yugabyte.yw.common.AlertTemplate.DB_CORE_FILES;
import static com.yugabyte.yw.common.AlertTemplate.DB_ERROR_LOGS;
import static com.yugabyte.yw.common.AlertTemplate.DB_FATAL_LOGS;
import static com.yugabyte.yw.common.AlertTemplate.DB_INSTANCE_DOWN;
import static com.yugabyte.yw.common.AlertTemplate.DB_INSTANCE_RESTART;
import static com.yugabyte.yw.common.AlertTemplate.DB_MEMORY_OVERLOAD;
import static com.yugabyte.yw.common.AlertTemplate.DB_QUEUES_OVERFLOW;
import static com.yugabyte.yw.common.AlertTemplate.DB_REDIS_CONNECTION;
import static com.yugabyte.yw.common.AlertTemplate.DB_VERSION_MISMATCH;
import static com.yugabyte.yw.common.AlertTemplate.DB_YCQL_CONNECTION;
import static com.yugabyte.yw.common.AlertTemplate.DB_YSQL_CONNECTION;
import static com.yugabyte.yw.common.AlertTemplate.HEALTH_CHECK_ERROR;
import static com.yugabyte.yw.common.AlertTemplate.HEALTH_CHECK_NOTIFICATION_ERROR;
import static com.yugabyte.yw.common.AlertTemplate.HIGH_NUM_YCQL_CONNECTIONS;
import static com.yugabyte.yw.common.AlertTemplate.HIGH_NUM_YEDIS_CONNECTIONS;
import static com.yugabyte.yw.common.AlertTemplate.INACTIVE_CRON_NODES;
import static com.yugabyte.yw.common.AlertTemplate.MEMORY_CONSUMPTION;
import static com.yugabyte.yw.common.AlertTemplate.NODE_CPU_USAGE;
import static com.yugabyte.yw.common.AlertTemplate.NODE_DISK_USAGE;
import static com.yugabyte.yw.common.AlertTemplate.NODE_DOWN;
import static com.yugabyte.yw.common.AlertTemplate.NODE_FILE_DESCRIPTORS_USAGE;
import static com.yugabyte.yw.common.AlertTemplate.NODE_OOM_KILLS;
import static com.yugabyte.yw.common.AlertTemplate.NODE_RESTART;
import static com.yugabyte.yw.common.AlertTemplate.NODE_TO_NODE_CA_CERT_EXPIRY;
import static com.yugabyte.yw.common.AlertTemplate.NODE_TO_NODE_CERT_EXPIRY;
import static com.yugabyte.yw.common.AlertTemplate.REPLICATION_LAG;
import static com.yugabyte.yw.common.AlertTemplate.YCQL_OP_AVG_LATENCY;
import static com.yugabyte.yw.common.AlertTemplate.YCQL_OP_P99_LATENCY;
import static com.yugabyte.yw.common.AlertTemplate.YCQL_THROUGHPUT;
import static com.yugabyte.yw.common.AlertTemplate.YSQL_OP_AVG_LATENCY;
import static com.yugabyte.yw.common.AlertTemplate.YSQL_OP_P99_LATENCY;
import static com.yugabyte.yw.common.AlertTemplate.YSQL_THROUGHPUT;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.*;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.AlertTemplateSettingsFormData;
import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.forms.filters.AlertTemplateApiFilter;
import com.yugabyte.yw.forms.paging.AlertConfigurationPagedApiQuery;
import com.yugabyte.yw.forms.paging.AlertPagedApiQuery;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.SortBy;
import com.yugabyte.yw.models.AlertConfiguration.TargetType;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedResponse;
import com.yugabyte.yw.models.paging.AlertPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class AlertControllerTest extends FakeDBApplication {

  private static final Map<AlertTemplate, String> TEST_ALERT_MESSAGE =
      ImmutableMap.<AlertTemplate, String>builder()
          .put(
              REPLICATION_LAG,
              "Average replication lag for universe 'Test Universe'"
                  + " is above 180000 ms. Current value is 180001 ms")
          .put(
              CLOCK_SKEW,
              "Max clock skew for universe 'Test Universe'"
                  + " is above 500 ms. Current value is 501 ms")
          .put(
              MEMORY_CONSUMPTION,
              "Average memory usage for universe 'Test Universe'"
                  + " is above 90%. Current value is 91%")
          .put(
              HEALTH_CHECK_ERROR,
              "Failed to perform health check for universe 'Test Universe'"
                  + " - check YB Platform logs for details or contact YB support team")
          .put(
              HEALTH_CHECK_NOTIFICATION_ERROR,
              "Failed to perform health check notification"
                  + " for universe 'Test Universe' - check Health notification settings and"
                  + " YB Platform logs for details or contact YB support team")
          .put(
              BACKUP_FAILURE,
              "Last backup task for universe 'Test Universe' failed"
                  + " - check backup task result for more details")
          .put(
              BACKUP_SCHEDULE_FAILURE,
              "Last attempt to run scheduled backup for universe"
                  + " 'Test Universe' failed due to other backup or universe operation is"
                  + " in progress.")
          .put(INACTIVE_CRON_NODES, "1 node(s) has inactive cronjob for universe 'Test Universe'.")
          .put(
              ALERT_QUERY_FAILED,
              "Last alert query for customer 'test@customer.com' failed"
                  + " - check YB Platform logs for details or contact YB support team")
          .put(
              ALERT_CONFIG_WRITING_FAILED,
              "Last alert rules sync for customer 'test@customer.com' failed"
                  + " - check YB Platform logs for details or contact YB support team")
          .put(
              ALERT_NOTIFICATION_ERROR,
              "Last attempt to send alert notifications for customer "
                  + "'test@customer.com' failed - check YB Platform logs for details"
                  + " or contact YB support team")
          .put(
              ALERT_NOTIFICATION_CHANNEL_ERROR,
              "Last attempt to send alert notifications to"
                  + " channel 'Some Channel' failed - try sending test alert to get more details")
          .put(
              NODE_DOWN,
              "1 DB node(s) are down for more than 15 minutes" + " for universe 'Test Universe'.")
          .put(
              NODE_RESTART,
              "Universe 'Test Universe' DB node is restarted 3 times" + " during last 30 minutes")
          .put(
              NODE_CPU_USAGE,
              "Average node CPU usage for universe 'Test Universe' is above 95%" + " on 1 node(s).")
          .put(
              NODE_DISK_USAGE,
              "Node disk usage for universe 'Test Universe'" + " is above 70% on 1 node(s).")
          .put(
              NODE_FILE_DESCRIPTORS_USAGE,
              "Node file descriptors usage for universe"
                  + " 'Test Universe' is above 70% on 1 node(s).")
          .put(
              NODE_OOM_KILLS,
              "More than 3 OOM kills detected for universe 'Test Universe'" + " on 1 node(s).")
          .put(
              DB_VERSION_MISMATCH,
              "Version mismatch detected for universe 'Test Universe'"
                  + " for 1 Master/TServer instance(s).")
          .put(
              DB_INSTANCE_DOWN,
              "1 DB Master/TServer instance(s) are down for more than"
                  + " 15 minutes for universe 'Test Universe'.")
          .put(
              DB_INSTANCE_RESTART,
              "Universe 'Test Universe' Master or TServer is restarted"
                  + " 3 times during last 30 minutes")
          .put(
              DB_FATAL_LOGS,
              "Fatal logs detected for universe 'Test Universe' on "
                  + "1 Master/TServer instance(s).")
          .put(
              DB_ERROR_LOGS,
              "Error logs detected for universe 'Test Universe' on "
                  + "1 Master/TServer instance(s).")
          .put(
              DB_CORE_FILES,
              "Core files detected for universe 'Test Universe' on " + "1 TServer instance(s).")
          .put(
              DB_YSQL_CONNECTION,
              "YSQLSH connection failure detected for universe 'Test Universe'"
                  + " on 1 TServer instance(s).")
          .put(
              DB_YCQL_CONNECTION,
              "CQLSH connection failure detected for universe 'Test Universe'"
                  + " on 1 TServer instance(s).")
          .put(
              DB_REDIS_CONNECTION,
              "Redis connection failure detected for universe 'Test Universe'"
                  + " on 1 TServer instance(s).")
          .put(DB_MEMORY_OVERLOAD, "DB memory rejections detected for universe 'Test Universe'.")
          .put(
              DB_COMPACTION_OVERLOAD,
              "DB compaction rejections detected for universe" + " 'Test Universe'.")
          .put(DB_QUEUES_OVERFLOW, "DB queues overflow detected for universe 'Test Universe'.")
          .put(
              NODE_TO_NODE_CA_CERT_EXPIRY,
              "Node to node CA certificate for universe"
                  + " 'Test Universe' will expire in 29 days.")
          .put(
              NODE_TO_NODE_CERT_EXPIRY,
              "Node to node certificate for universe 'Test Universe'" + " will expire in 29 days.")
          .put(
              CLIENT_TO_NODE_CA_CERT_EXPIRY,
              "Client to node CA certificate for universe"
                  + " 'Test Universe' will expire in 29 days.")
          .put(
              CLIENT_TO_NODE_CERT_EXPIRY,
              "Client to node certificate for universe 'Test Universe'"
                  + " will expire in 29 days.")
          .put(
              YSQL_OP_AVG_LATENCY,
              "Average YSQL operations latency for universe 'Test Universe'"
                  + " is above 10000 ms. Current value is 10001 ms")
          .put(
              YCQL_OP_AVG_LATENCY,
              "Average YCQL operations latency for universe 'Test Universe'"
                  + " is above 10000 ms. Current value is 10001 ms")
          .put(
              YSQL_OP_P99_LATENCY,
              "YSQL P99 latency for universe 'Test Universe'"
                  + " is above 60000 ms. Current value is 60001 ms")
          .put(
              YCQL_OP_P99_LATENCY,
              "YCQL P99 latency for universe 'Test Universe'"
                  + " is above 60000 ms. Current value is 60001 ms")
          .put(
              HIGH_NUM_YCQL_CONNECTIONS,
              "Number of YCQL connections for universe"
                  + " 'Test Universe' is above 1000. Current value is 1001")
          .put(
              HIGH_NUM_YEDIS_CONNECTIONS,
              "Number of YEDIS connections for universe"
                  + " 'Test Universe' is above 1000. Current value is 1001")
          .put(
              YSQL_THROUGHPUT,
              "Maximum throughput for YSQL operations for universe"
                  + " 'Test Universe' is above 100000. Current value is 100001")
          .put(
              YCQL_THROUGHPUT,
              "Maximum throughput for YCQL operations for universe"
                  + " 'Test Universe' is above 100000. Current value is 100001")
          .build();

  private Customer customer;

  private Users user;

  private String authToken;

  private Universe universe;

  @InjectMocks private AlertController controller;

  private SmtpData defaultSmtp = EmailFixtures.createSmtpData();

  private int alertChannelIndex;

  private int alertDestinationIndex;

  private AlertChannelService alertChannelService;
  private AlertDestinationService alertDestinationService;
  private AlertConfigurationService alertConfigurationService;
  private AlertController alertController;

  private AlertConfiguration alertConfiguration;
  private AlertDefinition alertDefinition;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    universe = ModelFactory.createUniverse();

    alertChannelService = app.injector().instanceOf(AlertChannelService.class);
    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
    alertConfigurationService = app.injector().instanceOf(AlertConfigurationService.class);
    alertController = app.injector().instanceOf(AlertController.class);
    alertConfiguration = ModelFactory.createAlertConfiguration(customer, universe);
    alertDefinition = ModelFactory.createAlertDefinition(customer, universe, alertConfiguration);
  }

  private void checkEmptyAnswer(String url) {
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), equalTo(OK));
    assertThat(contentAsString(result), equalTo("[]"));
  }

  private AlertChannelParams getAlertChannelParamsForTests() {
    AlertChannelEmailParams arParams = new AlertChannelEmailParams();
    arParams.setRecipients(Collections.singletonList("test@test.com"));
    arParams.setSmtpData(defaultSmtp);
    return arParams;
  }

  private ObjectNode getAlertChannelJson() {
    ObjectNode data = Json.newObject();
    data.put("name", getAlertChannelName());
    data.put("params", Json.toJson(getAlertChannelParamsForTests()));
    return data;
  }

  private AlertChannel channelFromJson(JsonNode json) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.treeToValue(json, AlertChannel.class);
    } catch (JsonProcessingException e) {
      fail("Bad json format.");
      return null;
    }
  }

  private AlertChannel createAlertChannel() {
    ObjectNode channelFormDataJson = getAlertChannelJson();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_channels",
            authToken,
            channelFormDataJson);
    assertThat(result.status(), equalTo(OK));
    return channelFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAndListAlertChannel_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");

    AlertChannel createdChannel = createAlertChannel();
    assertThat(createdChannel.getUuid(), notNullValue());

    assertThat(
        AlertUtils.getJsonTypeName(createdChannel.getParams()), equalTo(ChannelType.Email.name()));
    assertThat(createdChannel.getParams(), equalTo(getAlertChannelParamsForTests()));

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alert_channels", authToken);

    assertThat(result.status(), equalTo(OK));
    JsonNode listedChannels = Json.parse(contentAsString(result));
    assertThat(listedChannels.size(), equalTo(1));
    assertThat(
        channelFromJson(listedChannels.get(0)), equalTo(CommonUtils.maskObject(createdChannel)));
  }

  @Test
  public void testCreateAlertChannel_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");
    ObjectNode data = Json.newObject();
    data.put("name", "name");
    data.put("params", Json.toJson(new AlertChannelEmailParams()));
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.getUuid() + "/alert_channels",
                    authToken,
                    data));

    AssertHelper.assertBadRequest(
        result, "{\"params\":[\"only one of defaultRecipients and recipients[] should be set.\"]}");
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");
  }

  @Test
  public void testGetAlertChannel_OkResult() {
    AlertChannel createdChannel = createAlertChannel();
    assertThat(createdChannel.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.getUuid() + "/alert_channels/" + createdChannel.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    AlertChannel channel = channelFromJson(Json.parse(contentAsString(result)));
    assertThat(channel, notNullValue());
    assertThat(channel, equalTo(CommonUtils.maskObject(createdChannel)));
  }

  @Test
  public void testGetAlertChannel_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.getUuid() + "/alert_channels/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Channel UUID: " + uuid.toString());
  }

  @Test
  public void testUpdateAlertChannel_OkResult() {
    AlertChannel createdChannel = createAlertChannel();
    assertThat(createdChannel.getUuid(), notNullValue());

    AlertChannelEmailParams params = (AlertChannelEmailParams) createdChannel.getParams();
    params.setRecipients(Collections.singletonList("new@test.com"));
    params.getSmtpData().smtpPort = 1111;
    createdChannel.setParams(params);

    ObjectNode data = Json.newObject();
    data.put("alertChannelUUID", createdChannel.getUuid().toString())
        .put("name", createdChannel.getName())
        .put("params", Json.toJson(createdChannel.getParams()));

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_channels/"
                + createdChannel.getUuid().toString(),
            authToken,
            data);
    assertThat(result.status(), equalTo(OK));

    AlertChannel updatedChannel = channelFromJson(Json.parse(contentAsString(result)));

    assertThat(updatedChannel, notNullValue());
    assertThat(updatedChannel, equalTo(CommonUtils.maskObject(createdChannel)));
  }

  @Test
  public void testUpdateAlertChannel_ErrorResult() {
    AlertChannel createdChannel = createAlertChannel();
    assertThat(createdChannel.getUuid(), notNullValue());

    createdChannel.setParams(new AlertChannelSlackParams());

    ObjectNode data = Json.newObject();
    data.put("alertChannelUUID", createdChannel.getUuid().toString())
        .put("name", createdChannel.getName())
        .put("params", Json.toJson(createdChannel.getParams()));

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_channels/"
                        + createdChannel.getUuid().toString(),
                    authToken,
                    data));
    AssertHelper.assertBadRequest(
        result,
        "{\"params.webhookUrl\":[\"may not be null\"],"
            + "\"params.username\":[\"may not be null\"]}");
  }

  @Test
  public void testDeleteAlertChannel_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");

    AlertChannel createdChannel = createAlertChannel();
    assertThat(createdChannel.getUuid(), notNullValue());

    Metric channelStatus =
        metricService
            .buildMetricTemplate(
                PlatformMetrics.ALERT_MANAGER_CHANNEL_STATUS,
                MetricService.DEFAULT_METRIC_EXPIRY_SEC)
            .setCustomerUUID(customer.getUuid())
            .setSourceUuid(createdChannel.getUuid())
            .setLabels(MetricLabelsBuilder.create().appendSource(createdChannel).getMetricLabels())
            .setValue(0.0);
    metricService.save(channelStatus);

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_channels/"
                + createdChannel.getUuid().toString(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");

    AssertHelper.assertMetricValue(
        metricService,
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_CHANNEL_STATUS.getMetricName())
            .sourceUuid(createdChannel.getUuid())
            .build(),
        null);
  }

  @Test
  public void testDeleteAlertChannel_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/" + customer.getUuid() + "/alert_channels/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Channel UUID: " + uuid.toString());
  }

  @Test
  public void testDeleteAlertChannel_LastChannelInDestination_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_channels");

    AlertDestination firstDestination = createAlertDestination(false);
    assertThat(firstDestination.getUuid(), notNullValue());

    AlertDestination secondDestination = createAlertDestination(false);
    assertThat(secondDestination.getUuid(), notNullValue());

    // Updating second destination to have the same destinations.
    List<AlertChannel> channels = firstDestination.getChannelsList();
    secondDestination.setChannelsList(channels);
    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_destinations/"
                + secondDestination.getUuid().toString(),
            authToken,
            Json.toJson(secondDestination));
    assertThat(result.status(), is(OK));

    result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_channels/"
                + channels.get(0).getUuid().toString(),
            authToken);
    assertThat(result.status(), is(OK));

    result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_channels/"
                        + channels.get(1).getUuid().toString(),
                    authToken));

    AssertHelper.assertBadRequest(
        result,
        String.format(
            "Unable to delete alert channel: %s. 2 alert destinations have it as a last channel."
                + " Examples: [%s, %s]",
            channels.get(1).getUuid(), firstDestination.getName(), secondDestination.getName()));
  }

  private ObjectNode getAlertDestinationJson(boolean isDefault) {
    AlertChannel channel1 =
        ModelFactory.createEmailChannel(customer.getUuid(), getAlertChannelName());
    AlertChannel channel2 =
        ModelFactory.createSlackChannel(customer.getUuid(), getAlertChannelName());

    ObjectNode data = Json.newObject();
    data.put("name", getAlertDestinationName())
        .put("defaultDestination", Boolean.valueOf(isDefault))
        .putArray("channels")
        .add(channel1.getUuid().toString())
        .add(channel2.getUuid().toString());
    return data;
  }

  private AlertDestination destinationFromJson(JsonNode json) {
    ObjectMapper mapper = new ObjectMapper();
    List<UUID> channelUUIDs;
    try {
      channelUUIDs = Arrays.asList(mapper.readValue(json.get("channels").traverse(), UUID[].class));
      List<AlertChannel> channels =
          channelUUIDs
              .stream()
              .map(uuid -> alertChannelService.getOrBadRequest(customer.getUuid(), uuid))
              .collect(Collectors.toList());

      AlertDestination destination = new AlertDestination();
      destination.setUuid(UUID.fromString(json.get("uuid").asText()));
      destination.setName(json.get("name").asText());
      destination.setCustomerUUID(UUID.fromString(json.get("customerUUID").asText()));
      destination.setChannelsList(channels);
      destination.setDefaultDestination(json.get("defaultDestination").asBoolean());
      return destination;
    } catch (IOException e) {
      return null;
    }
  }

  private AlertDestination createAlertDestination(boolean isDefault) {
    ObjectNode destinationFormDataJson = getAlertDestinationJson(isDefault);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_destinations",
            authToken,
            destinationFormDataJson);
    assertThat(result.status(), equalTo(OK));
    return destinationFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAlertDestination_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination createdDestination = createAlertDestination(false);
    assertThat(createdDestination.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alert_destinations", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode listedDestinations = Json.parse(contentAsString(result));
    assertThat(listedDestinations.size(), equalTo(1));
    assertThat(destinationFromJson(listedDestinations.get(0)), equalTo(createdDestination));
  }

  @Test
  public void testCreateAlertDestination_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");
    ObjectNode data = Json.newObject();
    String alertChannelUUID = UUID.randomUUID().toString();
    data.put("name", getAlertDestinationName())
        .put("defaultDestination", Boolean.FALSE)
        .putArray("channels")
        .add(alertChannelUUID);
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.getUuid() + "/alert_destinations",
                    authToken,
                    data));

    AssertHelper.assertBadRequest(result, "Invalid Alert Channel UUID: " + alertChannelUUID);
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");
  }

  @Test
  public void testCreateAlertDestinationWithDefaultChange() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination firstDestination = createAlertDestination(true);
    assertThat(firstDestination.getUuid(), notNullValue());
    assertThat(
        alertDestinationService.getDefaultDestination(customer.uuid), equalTo(firstDestination));

    AlertDestination secondDestination = createAlertDestination(true);
    assertThat(secondDestination.getUuid(), notNullValue());
    assertThat(
        alertDestinationService.getDefaultDestination(customer.uuid), equalTo(secondDestination));
  }

  @Test
  public void testGetAlertDestination_OkResult() {
    AlertDestination createdDestination = createAlertDestination(false);
    assertThat(createdDestination.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_destinations/"
                + createdDestination.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    AlertDestination destination = destinationFromJson(Json.parse(contentAsString(result)));
    assertThat(destination, notNullValue());
    assertThat(destination, equalTo(createdDestination));
  }

  @Test
  public void testGetAlertDestination_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_destinations/"
                        + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Destination UUID: " + uuid.toString());
  }

  @Test
  public void testUpdateAlertDestination_AnotherDefaultDestination() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination firstDestination = createAlertDestination(true);
    assertThat(firstDestination.getUuid(), notNullValue());
    assertThat(
        alertDestinationService.getDefaultDestination(customer.uuid), equalTo(firstDestination));

    AlertDestination secondDestination = createAlertDestination(false);
    assertThat(secondDestination.getUuid(), notNullValue());
    // To be sure the default destination hasn't been changed.
    assertThat(
        alertDestinationService.getDefaultDestination(customer.uuid), equalTo(firstDestination));

    secondDestination.setDefaultDestination(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_destinations/"
                + secondDestination.getUuid().toString(),
            authToken,
            Json.toJson(secondDestination));
    assertThat(result.status(), is(OK));
    AlertDestination receivedDestination = destinationFromJson(Json.parse(contentAsString(result)));

    assertThat(receivedDestination.isDefaultDestination(), is(true));
    assertThat(
        alertDestinationService.getDefaultDestination(customer.uuid), equalTo(secondDestination));
  }

  @Test
  public void testUpdateAlertDestination_ChangeDefaultFlag_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination destination = createAlertDestination(true);
    assertThat(destination.getUuid(), notNullValue());
    assertThat(alertDestinationService.getDefaultDestination(customer.uuid), equalTo(destination));

    destination.setDefaultDestination(false);
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_destinations/"
                        + destination.getUuid().toString(),
                    authToken,
                    Json.toJson(destination)));
    AssertHelper.assertBadRequest(
        result,
        "{\"defaultDestination\":[\"can't set the alert destination as non-default - "
            + "make another destination as default at first.\"]}");
    destination.setDefaultDestination(true);
    assertThat(alertDestinationService.getDefaultDestination(customer.uuid), equalTo(destination));
  }

  @Test
  public void testDeleteAlertDestination_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination createdDestination = createAlertDestination(false);
    assertThat(createdDestination.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_destinations/"
                + createdDestination.getUuid().toString(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");
  }

  @Test
  public void testDeleteAlertDestination_InvalidUUID_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_destinations/"
                        + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Destination UUID: " + uuid.toString());
  }

  @Test
  public void testDeleteAlertDestination_DefaultDestination_ErrorResult() {
    AlertDestination createdDestination = createAlertDestination(true);
    String destinationUUID = createdDestination.getUuid().toString();

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_destinations/"
                        + destinationUUID,
                    authToken));
    AssertHelper.assertBadRequest(
        result,
        "Unable to delete default alert destination '"
            + createdDestination.getName()
            + "', make another destination default at first.");
  }

  @Test
  public void testListAlertDestinations_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alert_destinations");

    AlertDestination createdDestination1 = createAlertDestination(false);
    AlertDestination createdDestination2 = createAlertDestination(false);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alert_destinations", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode listedDestinations = Json.parse(contentAsString(result));
    assertThat(listedDestinations.size(), equalTo(2));

    AlertDestination listedDestination1 = destinationFromJson(listedDestinations.get(0));
    AlertDestination listedDestination2 = destinationFromJson(listedDestinations.get(1));
    assertThat(listedDestination1, not(listedDestination2));
    assertThat(
        listedDestination1, anyOf(equalTo(createdDestination1), equalTo(createdDestination2)));
    assertThat(
        listedDestination2, anyOf(equalTo(createdDestination1), equalTo(createdDestination2)));
  }

  private String getAlertChannelName() {
    return "Test AlertChannel " + (alertChannelIndex++);
  }

  private String getAlertDestinationName() {
    return "Test AlertDestination " + (alertDestinationIndex++);
  }

  @Test
  public void testGetAlert() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alerts/" + initial.getUuid(), authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    Alert alert = Json.fromJson(alertsJson, Alert.class);

    assertThat(alert, equalTo(initial));
  }

  @Test
  public void testListAlerts() {
    checkEmptyAnswer("/api/customers/" + customer.getUuid() + "/alerts");
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alerts", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    List<Alert> alerts = Arrays.asList(Json.fromJson(alertsJson, Alert[].class));

    assertThat(alerts, hasSize(1));
    assertThat(alerts.get(0), equalTo(initial));
  }

  @Test
  public void testListActiveAlerts() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial2 = ModelFactory.createAlert(customer, alertDefinition);

    alertService.markResolved(AlertFilter.builder().uuid(initial2.getUuid()).build());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alerts/active", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    List<Alert> alerts = Arrays.asList(Json.fromJson(alertsJson, Alert[].class));

    assertThat(alerts, hasSize(1));
    assertThat(alerts.get(0), equalTo(initial));
  }

  @Test
  public void testCountAlerts() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);

    AlertApiFilter filter = new AlertApiFilter();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alerts/count",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    int alertCount = Json.fromJson(alertsJson, int.class);

    assertThat(alertCount, equalTo(1));
  }

  @Test
  public void testPageAlerts() {
    ModelFactory.createAlert(customer, alertDefinition);
    Alert initial2 = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial3 = ModelFactory.createAlert(customer, alertDefinition);

    initial2.setCreateTime(Date.from(initial2.getCreateTime().toInstant().minusSeconds(5))).save();
    initial3.setCreateTime(Date.from(initial3.getCreateTime().toInstant().minusSeconds(10))).save();

    AlertPagedApiQuery query = new AlertPagedApiQuery();
    query.setSortBy(Alert.SortBy.createTime);
    query.setDirection(PagedQuery.SortDirection.DESC);
    query.setFilter(new AlertApiFilter());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alerts/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    AlertPagedResponse alerts = Json.fromJson(alertsJson, AlertPagedResponse.class);

    assertThat(alerts.isHasNext(), is(false));
    assertThat(alerts.isHasPrev(), is(true));
    assertThat(alerts.getTotalCount(), equalTo(3));
    assertThat(alerts.getEntities(), hasSize(2));
    assertThat(alerts.getEntities(), contains(initial2, initial3));
  }

  @Test
  public void testAcknowledgeAlert() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);

    Result result =
        doRequestWithAuthToken(
            "POST",
            "/api/customers/" + customer.uuid + "/alerts/" + initial.getUuid() + "/acknowledge",
            authToken);
    assertThat(result.status(), equalTo(OK));

    JsonNode alertsJson = Json.parse(contentAsString(result));
    Alert acknowledged = Json.fromJson(alertsJson, Alert.class);
    if (!alertsJson.has("nextNotificationTime")) {
      acknowledged.setNextNotificationTime(null);
    }

    initial.setState(Alert.State.ACKNOWLEDGED);
    initial.setAcknowledgedTime(acknowledged.getAcknowledgedTime());
    initial.setNotifiedState(Alert.State.ACKNOWLEDGED);
    initial.setNextNotificationTime(null);
    assertThat(acknowledged, equalTo(initial));
  }

  @Test
  public void testAcknowledgeAlerts() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);
    ModelFactory.createAlert(customer, alertDefinition);
    ModelFactory.createAlert(customer, alertDefinition);

    AlertApiFilter apiFilter = new AlertApiFilter();
    apiFilter.setUuids(ImmutableSet.of(initial.getUuid()));

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alerts/acknowledge",
            authToken,
            Json.toJson(apiFilter));
    assertThat(result.status(), equalTo(OK));

    Alert acknowledged = alertService.get(initial.getUuid());
    initial.setState(Alert.State.ACKNOWLEDGED);
    initial.setAcknowledgedTime(acknowledged.getAcknowledgedTime());
    initial.setNotifiedState(Alert.State.ACKNOWLEDGED);
    initial.setNextNotificationTime(null);
    assertThat(acknowledged, equalTo(initial));
  }

  @Test
  public void testListTemplates() {
    AlertTemplateApiFilter apiFilter = new AlertTemplateApiFilter();
    apiFilter.setName(AlertTemplate.MEMORY_CONSUMPTION.getName());

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_templates",
            authToken,
            Json.toJson(apiFilter));
    assertThat(result.status(), equalTo(OK));
    JsonNode templatesJson = Json.parse(contentAsString(result));
    List<AlertConfiguration> templates =
        Arrays.asList(Json.fromJson(templatesJson, AlertConfiguration[].class));

    assertThat(templates, hasSize(1));
    AlertConfiguration template = templates.get(0);
    assertThat(template.getName(), equalTo(AlertTemplate.MEMORY_CONSUMPTION.getName()));
    assertThat(template.getTemplate(), equalTo(AlertTemplate.MEMORY_CONSUMPTION));
    assertThat(
        template.getDescription(), equalTo(AlertTemplate.MEMORY_CONSUMPTION.getDescription()));
    assertThat(template.getTargetType(), equalTo(AlertTemplate.MEMORY_CONSUMPTION.getTargetType()));
    assertThat(template.getTarget(), equalTo(new AlertConfigurationTarget().setAll(true)));
    assertThat(
        template.getThresholdUnit(),
        equalTo(AlertTemplate.MEMORY_CONSUMPTION.getDefaultThresholdUnit()));
    assertThat(
        template.getThresholds(),
        equalTo(
            ImmutableMap.of(
                AlertConfiguration.Severity.SEVERE,
                new AlertConfigurationThreshold()
                    .setCondition(Condition.GREATER_THAN)
                    .setThreshold(90D))));
    assertThat(
        template.getDurationSec(),
        equalTo(AlertTemplate.MEMORY_CONSUMPTION.getDefaultDurationSec()));
  }

  @Test
  public void testGetConfigurationSuccess() {
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_configurations/"
                + alertConfiguration.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationJson = Json.parse(contentAsString(result));
    AlertConfiguration configuration = Json.fromJson(configurationJson, AlertConfiguration.class);

    assertThat(configuration, equalTo(alertConfiguration));
  }

  @Test
  public void testGetConfigurationFailure() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.getUuid() + "/alert_configurations/" + uuid,
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Configuration UUID: " + uuid);
  }

  @Test
  public void testPageConfigurations() {
    AlertConfiguration configuration2 = ModelFactory.createAlertConfiguration(customer, universe);
    AlertConfiguration configuration3 = ModelFactory.createAlertConfiguration(customer, universe);

    configuration2
        .setCreateTime(Date.from(configuration2.getCreateTime().toInstant().minusSeconds(5)))
        .save();
    configuration3
        .setCreateTime(Date.from(configuration3.getCreateTime().toInstant().minusSeconds(10)))
        .save();

    AlertConfigurationPagedApiQuery query = new AlertConfigurationPagedApiQuery();
    query.setSortBy(SortBy.createTime);
    query.setDirection(PagedQuery.SortDirection.DESC);
    query.setFilter(new AlertConfigurationApiFilter());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_configurations/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationsJson = Json.parse(contentAsString(result));
    AlertConfigurationPagedResponse configurations =
        Json.fromJson(configurationsJson, AlertConfigurationPagedResponse.class);

    assertThat(configurations.isHasNext(), is(false));
    assertThat(configurations.isHasPrev(), is(true));
    assertThat(configurations.getTotalCount(), equalTo(3));
    assertThat(configurations.getEntities(), hasSize(2));
    assertThat(configurations.getEntities(), contains(configuration2, configuration3));
  }

  @Ignore("See PLAT-545 why we cannot fail on unknown params")
  public void testListConfigurations_unknown_filter_props() {
    JsonNode badFilter =
        Json.parse(
            "{\n" + "\"jatin\": 3,\n" + "\"alexander\": \"bar\",\n" + "\"shashank\": null\n" + "}");
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_configurations/list",
            authToken,
            Json.toJson(badFilter));
    assertBadRequest(result, "unknown fields error");
  }

  @Test
  public void testListConfigurations() {
    AlertConfiguration configuration2 = ModelFactory.createAlertConfiguration(customer, universe);
    AlertConfiguration configuration3 = ModelFactory.createAlertConfiguration(customer, universe);

    configuration3.setActive(false);
    alertConfigurationService.save(configuration3);

    AlertConfigurationApiFilter filter = new AlertConfigurationApiFilter();
    filter.setActive(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_configurations/list",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationsJson = Json.parse(contentAsString(result));
    List<AlertConfiguration> configurations =
        Arrays.asList(Json.fromJson(configurationsJson, AlertConfiguration[].class));

    assertThat(configurations, hasSize(2));
    assertThat(configurations, containsInAnyOrder(alertConfiguration, configuration2));
  }

  @Test
  public void testCreateConfiguration() {
    AlertDestination destination = createAlertDestination(false);
    alertConfiguration.setUuid(null);
    alertConfiguration.setCreateTime(null);
    alertConfiguration.setDestinationUUID(destination.getUuid());
    alertConfiguration.setDefaultDestination(false);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/alert_configurations",
            authToken,
            Json.toJson(alertConfiguration));
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationJson = Json.parse(contentAsString(result));
    AlertConfiguration configuration = Json.fromJson(configurationJson, AlertConfiguration.class);

    assertThat(configuration.getUuid(), notNullValue());
    assertThat(configuration.getCreateTime(), notNullValue());
    assertThat(configuration.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(configuration.getName(), equalTo("alertConfiguration"));
    assertThat(configuration.getTemplate(), equalTo(AlertTemplate.MEMORY_CONSUMPTION));
    assertThat(configuration.getDescription(), equalTo("alertConfiguration description"));
    assertThat(configuration.getTargetType(), equalTo(AlertConfiguration.TargetType.UNIVERSE));
    assertThat(
        configuration.getTarget(),
        equalTo(
            new AlertConfigurationTarget().setUuids(ImmutableSet.of(universe.getUniverseUUID()))));
    assertThat(configuration.getThresholdUnit(), equalTo(Unit.PERCENT));
    assertThat(
        configuration.getThresholds(),
        equalTo(
            ImmutableMap.of(
                AlertConfiguration.Severity.SEVERE,
                new AlertConfigurationThreshold()
                    .setCondition(Condition.GREATER_THAN)
                    .setThreshold(1D))));
    assertThat(configuration.getDurationSec(), equalTo(0));
    assertThat(configuration.getDestinationUUID(), equalTo(destination.getUuid()));
  }

  @Test
  public void testCreateConfigurationFailure() {
    alertConfiguration.setUuid(null);
    alertConfiguration.setName(null);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.getUuid() + "/alert_configurations",
                    authToken,
                    Json.toJson(alertConfiguration)));
    assertBadRequest(result, "{\"name\":[\"may not be null\"]}");
  }

  @Test
  public void testUpdateConfiguration() {
    AlertDestination destination = createAlertDestination(false);
    alertConfiguration.setDestinationUUID(destination.getUuid());
    alertConfiguration.setDefaultDestination(false);

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_configurations/"
                + alertConfiguration.getUuid(),
            authToken,
            Json.toJson(alertConfiguration));
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationJson = Json.parse(contentAsString(result));
    AlertConfiguration configuration = Json.fromJson(configurationJson, AlertConfiguration.class);

    assertThat(configuration.getDestinationUUID(), equalTo(destination.getUuid()));
  }

  @Test
  public void testUpdateConfigurationFailure() {
    alertConfiguration.setTargetType(null);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/alert_configurations/"
                        + alertConfiguration.getUuid(),
                    authToken,
                    Json.toJson(alertConfiguration)));
    assertBadRequest(result, "{\"targetType\":[\"may not be null\"]}");
  }

  @Test
  public void testDeleteConfiguration() {
    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_configurations/"
                + alertConfiguration.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }

  @Test
  public void testSendTestAlert() throws IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url("/some/path");
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

      AlertChannel channel = new AlertChannel();
      channel.setName("Some channel");
      channel.setCustomerUUID(customer.getUuid());
      AlertChannelSlackParams params = new AlertChannelSlackParams();
      params.setUsername("Slack Bot");
      params.setWebhookUrl(baseUrl.toString());
      channel.setParams(params);

      alertChannelService.save(channel);

      AlertDestination destination = new AlertDestination();
      destination.setCustomerUUID(customer.getUuid());
      destination.setName("Some destination");
      destination.setChannelsList(ImmutableList.of(channel));

      alertDestinationService.save(destination);

      alertConfiguration.setDestinationUUID(destination.getUuid());
      alertConfiguration.setDefaultDestination(false);
      alertConfigurationService.save(alertConfiguration);

      Result result =
          doRequestWithAuthToken(
              "POST",
              "/api/customers/"
                  + customer.getUuid()
                  + "/alert_configurations/"
                  + alertConfiguration.getUuid()
                  + "/test_alert",
              authToken);
      assertThat(result.status(), equalTo(OK));
      JsonNode resultJson = Json.parse(contentAsString(result));
      assertThat(
          resultJson.get("message").asText(),
          equalTo("Result: Some channel - Alert sent successfully"));
      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is("/some/path"));
      assertThat(
          request.getBody().readString(Charset.defaultCharset()),
          equalTo(
              "{\"username\":\"Slack Bot\","
                  + "\"text\":\"alertConfiguration alert with severity level 'SEVERE' "
                  + "for universe 'Test Universe' is firing.\\n"
                  + "\\n[TEST ALERT!!!] Average memory usage for universe 'Test Universe' "
                  + "is above 1%. Current value is 2%\",\"icon_url\":null}"));
    }
  }

  @Test
  public void testListTemplateSettings() {
    ModelFactory.createTemplateSettings(customer);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/alert_template_settings", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode settingsJson = Json.parse(contentAsString(result));
    List<AlertTemplateSettings> queriedSettings =
        Arrays.asList(Json.fromJson(settingsJson, AlertTemplateSettings[].class));

    assertThat(queriedSettings, hasSize(1));
    AlertTemplateSettings settings = queriedSettings.get(0);
    assertThat(settings.getUuid(), notNullValue());
    assertThat(settings.getCreateTime(), notNullValue());
    assertThat(settings.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(settings.getTemplate(), equalTo(AlertTemplate.MEMORY_CONSUMPTION.name()));
    assertThat(
        settings.getLabels().entrySet(),
        everyItem(is(in(ImmutableMap.of("foo", "bar", "one", "two").entrySet()))));
  }

  @Test
  public void testCreateTemplateSettings() {
    AlertTemplateSettings templateSettings =
        new AlertTemplateSettings()
            .setCustomerUUID(customer.getUuid())
            .setTemplate(MEMORY_CONSUMPTION.name())
            .setLabels(ImmutableMap.of("foo", "bar"));
    AlertTemplateSettingsFormData data = new AlertTemplateSettingsFormData();
    data.settings = ImmutableList.of(templateSettings);
    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/" + customer.getUuid() + "/alert_template_settings",
            authToken,
            Json.toJson(data));
    assertThat(result.status(), equalTo(OK));
    JsonNode settingsJson = Json.parse(contentAsString(result));
    List<AlertTemplateSettings> createdSettings =
        Arrays.asList(Json.fromJson(settingsJson, AlertTemplateSettings[].class));

    assertThat(createdSettings, hasSize(1));
    AlertTemplateSettings settings = createdSettings.get(0);
    assertThat(settings.getUuid(), notNullValue());
    assertThat(settings.getCreateTime(), notNullValue());
    assertThat(settings.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(settings.getTemplate(), equalTo(AlertTemplate.MEMORY_CONSUMPTION.name()));
    assertThat(
        settings.getLabels().entrySet(),
        everyItem(is(in(ImmutableMap.of("foo", "bar").entrySet()))));
  }

  @Test
  public void testCreateTemplateSettingsFailure() {
    AlertTemplateSettings templateSettings =
        new AlertTemplateSettings()
            .setCustomerUUID(customer.getUuid())
            .setTemplate("fake")
            .setLabels(ImmutableMap.of("foo", "bar"));
    AlertTemplateSettingsFormData data = new AlertTemplateSettingsFormData();
    data.settings = ImmutableList.of(templateSettings);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/" + customer.getUuid() + "/alert_template_settings",
                    authToken,
                    Json.toJson(data)));
    assertBadRequest(result, "{\"template\":[\"Template 'fake' is missing\"]}");
  }

  @Test
  public void testUpdateTemplateSettings() {
    AlertTemplateSettings templateSettings = ModelFactory.createTemplateSettings(customer);
    templateSettings.setLabels(ImmutableMap.of("foo", "bar"));

    AlertTemplateSettingsFormData data = new AlertTemplateSettingsFormData();
    data.settings = ImmutableList.of(templateSettings);
    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/" + customer.getUuid() + "/alert_template_settings",
            authToken,
            Json.toJson(data));
    assertThat(result.status(), equalTo(OK));
    JsonNode settingsJson = Json.parse(contentAsString(result));
    List<AlertTemplateSettings> updatedSettings =
        Arrays.asList(Json.fromJson(settingsJson, AlertTemplateSettings[].class));

    assertThat(updatedSettings, hasSize(1));
    AlertTemplateSettings settings = updatedSettings.get(0);
    assertThat(settings.getUuid(), equalTo(settings.getUuid()));
    assertThat(settings.getCreateTime(), equalTo(settings.getCreateTime()));
    assertThat(
        settings.getLabels().entrySet(),
        everyItem(is(in(ImmutableMap.of("foo", "bar").entrySet()))));
  }

  @Test
  public void testDeleteTemplateSettings() {
    AlertTemplateSettings settings = ModelFactory.createTemplateSettings(customer);
    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/alert_template_settings/"
                + settings.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }

  @Test
  public void testTestAlertMessage() {
    TEST_ALERT_MESSAGE.forEach(
        (template, message) -> {
          AlertConfiguration configuration =
              alertConfigurationService
                  .createConfigurationTemplate(customer, template)
                  .getDefaultConfiguration();
          if (configuration.getTargetType() == TargetType.UNIVERSE) {
            configuration.setTarget(
                new AlertConfigurationTarget()
                    .setAll(false)
                    .setUuids(ImmutableSet.of(universe.getUniverseUUID())));
          }
          alertConfigurationService.save(configuration);
          Alert testAlert = alertController.createTestAlert(configuration);
          assertThat(testAlert.getMessage(), CoreMatchers.equalTo("[TEST ALERT!!!] " + message));
        });
  }
}
