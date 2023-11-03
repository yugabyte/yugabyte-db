package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.ExternalScriptHelper;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class ScheduleScriptControllerTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe defaultUniverse;

  private final String validScriptParam =
      "{\"params1\" : \"val1\", \"params2\" : \"val2\", \"params3\": \"val3\"}";

  int OK = 200;

  private Config mockConfig;

  @Override
  protected Application provideApplication() {
    mockConfig = mock(Config.class);
    // when(mockConfig.getString(anyString())).thenReturn("");
    return super.configureApplication(
            new GuiceApplicationBuilder().disable(GuiceModule.class).configure(testDatabase()))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  @Before
  public void setUp() throws Exception {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    Users defaultUser = ModelFactory.testUser(defaultCustomer);
    when(mockConfig.getBoolean(ExternalScriptHelper.EXT_SCRIPT_ACCESS_FULL_PATH)).thenReturn(true);
  }

  private Result createScriptSchedule(
      UUID universeUUID,
      String cronExpression,
      String scriptParam,
      String timeLimitMins,
      boolean addScript) {
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
    if (cronExpression != null) {
      bodyData.add(new Http.MultipartFormData.DataPart("cronExpression", cronExpression));
    }
    if (scriptParam != null) {
      bodyData.add(new Http.MultipartFormData.DataPart("scriptParameter", scriptParam));
    }
    if (timeLimitMins != null) {
      bodyData.add(new Http.MultipartFormData.DataPart("timeLimitMins", timeLimitMins));
    }
    if (addScript) {
      String tmpFile = createTempFile("User defined Script");
      Source<ByteString, ?> scriptFile = FileIO.fromFile(new File(tmpFile));
      bodyData.add(
          new Http.MultipartFormData.FilePart<>(
              "script", "test.py", "application/octet-stream", scriptFile));
    }
    String url =
        "/api/v1/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/schedule_script";
    return doRequestWithMultipartData("POST", url, bodyData, mat);
  }

  @Test
  public void testWithoutCronExpression() {
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(), null, validScriptParam, "5", true));
    assertBadRequest(result, "No cronExpression found");
  }

  @Test
  public void testWithInvalidCronExpression() {
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(),
                    "* * * * ? * *",
                    validScriptParam,
                    "5",
                    true));
    assertBadRequest(result, "Please provide a valid cronExpression");
  }

  @Test
  public void testWithoutTimeLimitMins() {
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, null, true));
    assertBadRequest(result, "Please provide valid timeLimitMins for script execution.");
  }

  @Test
  public void testWithInvalidTimeLimitMins() {
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(),
                    "5 * * * *",
                    validScriptParam,
                    "dummyValue",
                    true));
    assertBadRequest(result, "Please provide valid timeLimitMins for script execution.");
  }

  @Test
  public void testWithoutUploadingScript() {
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", false));
    assertBadRequest(result, "Script file not found");
  }

  @Test
  public void testWithUnkownUniverse() {
    UUID unKnownUniverseUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    unKnownUniverseUUID, "5 * * * *", validScriptParam, "5", true));
    assertBadRequest(result, "Cannot find universe");
  }

  @Test
  public void testCreateScriptSchedule() {
    Result result =
        createScriptSchedule(
            defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", true);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "status", "Active");
  }

  @Test
  public void testInvalidAccess() {
    when(mockConfig.getBoolean(ExternalScriptHelper.EXT_SCRIPT_ACCESS_FULL_PATH)).thenReturn(false);
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", true));
    assertBadRequest(result, "External Script APIs are disabled. Please contact support team");
  }

  @Test
  public void testCreateMultipleScriptSchedule() {
    createScriptSchedule(
        defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", true);
    Result result =
        assertPlatformException(
            () ->
                createScriptSchedule(
                    defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", true));
    assertBadRequest(result, "A External Script is already scheduled for this universe.");
  }

  @Test
  public void testModifyScriptSchedule() {
    createScriptSchedule(
        defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "1", true);
    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
    bodyData.add(new Http.MultipartFormData.DataPart("cronExpression", "2 * * * *"));
    bodyData.add(new Http.MultipartFormData.DataPart("timeLimitMins", "5"));
    String tmpFile = createTempFile("User defined Script");
    Source<ByteString, ?> scriptFile = FileIO.fromFile(new File(tmpFile));
    bodyData.add(
        new Http.MultipartFormData.FilePart<>(
            "script", "test.py", "application/octet-stream", scriptFile));
    String url =
        "/api/v1/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + defaultUniverse.getUniverseUUID()
            + "/update_scheduled_script";
    Result result = doRequestWithMultipartData("PUT", url, bodyData, mat);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "cronExpression", "2 * * * *");
  }

  @Test
  public void testStopScriptSchedule() {
    createScriptSchedule(
        defaultUniverse.getUniverseUUID(), "5 * * * *", validScriptParam, "5", true);
    String url =
        "/api/v1/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + defaultUniverse.getUniverseUUID()
            + "/stop_scheduled_script";
    Result result = doRequest("PUT", url);
    assertEquals(OK, result.status());
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "status", "Stopped");
  }
}
