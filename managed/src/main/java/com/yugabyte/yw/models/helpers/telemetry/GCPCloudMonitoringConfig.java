package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.services.cloudresourcemanager.CloudResourceManager;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsRequest;
import com.google.api.services.cloudresourcemanager.model.TestIamPermissionsResponse;
import com.google.api.services.iam.v1.IamScopes;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.common.YBADeprecated;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "GCPCloudMonitoringConfig Config")
@Slf4j
public class GCPCloudMonitoringConfig extends TelemetryProviderConfig {

  public static final List<String> REQUIRED_LOGGING_PERMISSIONS =
      Arrays.asList("logging.logEntries.create", "logging.logEntries.route");

  @ApiModelProperty(value = "Project ID", accessMode = READ_WRITE)
  private String project;

  /**
   * @deprecated Use {@link #credentialsString} (String) instead. JsonNode is not supported by
   *     generated API clients. This field is only accepted on input for backward compatibility;
   *     responses will only include credentialsString.
   */
  @Deprecated
  @YBADeprecated(sinceDate = "2026-02-12", sinceYBAVersion = "2026.1.0")
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2026.1.0")
  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2026.1.0.</b> Use"
              + " credentialsString instead.",
      accessMode = READ_WRITE)
  private JsonNode credentials;

  /**
   * GCP service account credentials as JSON string. Preferred over the deprecated {@link
   * #credentials} for API/CLI compatibility. When set via API, send the raw JSON content (e.g.
   * content of the service account key file).
   */
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2026.1.0")
  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. GCP service account credentials as"
              + " JSON string (content of the credentials JSON file)",
      accessMode = READ_WRITE)
  private String credentialsString;

  public GCPCloudMonitoringConfig() {
    setType(ProviderType.GCP_CLOUD_MONITORING);
  }

  /**
   * Returns credentials as JsonNode from either the deprecated {@link #credentials} field or by
   * parsing {@link #credentialsString}. Returns null if neither is set or credentialsString is
   * invalid.
   */
  public JsonNode getGcmCredentials() {
    if (credentials != null) {
      return credentials;
    }
    if (StringUtils.isBlank(credentialsString)) {
      return null;
    }
    try {
      return Json.parse(credentialsString);
    } catch (Exception e) {
      log.warn("Failed to parse credentialsString as JSON", e);
      return null;
    }
  }

  @Override
  public void validateConfigFields() {
    if (credentials != null && StringUtils.isNotBlank(credentialsString)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Provide only one of 'credentials' or 'credentialsString' - both cannot be set.");
    }
    JsonNode creds = getGcmCredentials();
    if (creds == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Validation failed. GCP credentials are required.");
    }
    if (StringUtils.isBlank(project) && (creds == null || !creds.hasNonNull("project_id"))) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Validation failed. Project is required for GCP Cloud Monitoring");
    }
  }

  @Override
  public void validateConnectivity(ApiHelper apiHelper) {
    JsonNode creds = getGcmCredentials();
    String project_id = StringUtils.isBlank(project) ? creds.get("project_id").asText() : project;

    CloudResourceManager service;
    TestIamPermissionsRequest requestBody =
        new TestIamPermissionsRequest().setPermissions(REQUIRED_LOGGING_PERMISSIONS);

    try {
      service = createCloudResourceManagerService(creds);

      // Try and test for the permissions.
      TestIamPermissionsResponse testIamPermissionsResponse =
          service.projects().testIamPermissions(project_id, requestBody).execute();

      // If response is null or invalid.
      if (testIamPermissionsResponse == null
          || testIamPermissionsResponse.getPermissions() == null) {
        throw new RuntimeException("Got invalid test permissions response from GCP.");
      }

      // If user has all of the required permissions.
      if (CollectionUtils.isEqualCollection(
          REQUIRED_LOGGING_PERMISSIONS, testIamPermissionsResponse.getPermissions())) {
        log.info(
            "Verified that the user has following permissions required for GCP Cloud Monitoring: "
                + String.join(", ", REQUIRED_LOGGING_PERMISSIONS));
        return;
      } else {
        // If user doesn't have enough permissions.
        String errMessage =
            "The user is missing the following permission(s) required for GCP Cloud Monitoring:"
                + " "
                + String.join(
                    ", ",
                    CollectionUtils.subtract(
                        REQUIRED_LOGGING_PERMISSIONS, testIamPermissionsResponse.getPermissions()));

        log.error(errMessage);
        throw new PlatformServiceException(BAD_REQUEST, errMessage);
      }
    } catch (PlatformServiceException pe) {
      // Bubble up the PlatformServiceExceptions as they are already handled.
      throw pe;
    } catch (Exception e) {
      log.error("Validation failed. Got an error while trying to test GCP permissions: \n", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Validation failed. Got an error while trying to test GCP permissions");
    }
  }

  public CloudResourceManager createCloudResourceManagerService(JsonNode credentials)
      throws IOException, GeneralSecurityException {

    GoogleCredentials credential =
        GoogleCredentials.fromStream(new ByteArrayInputStream(credentials.toString().getBytes()))
            .createScoped(Collections.singleton(IamScopes.CLOUD_PLATFORM));

    return new CloudResourceManager.Builder(
            GoogleNetHttpTransport.newTrustedTransport(),
            GsonFactory.getDefaultInstance(),
            new HttpCredentialsAdapter(credential))
        .setApplicationName("service-accounts")
        .build();
  }
}
