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
import com.yugabyte.yw.common.PlatformServiceException;
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

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "GCPCloudMonitoringConfig Config")
@Slf4j
public class GCPCloudMonitoringConfig extends TelemetryProviderConfig {

  public static final List<String> REQUIRED_LOGGING_PERMISSIONS =
      Arrays.asList("logging.logEntries.create", "logging.logEntries.route");

  @ApiModelProperty(value = "Project ID", accessMode = READ_WRITE)
  private String project;

  @ApiModelProperty(value = "Credentials", accessMode = READ_WRITE)
  private JsonNode credentials;

  public GCPCloudMonitoringConfig() {
    setType(ProviderType.GCP_CLOUD_MONITORING);
  }

  @Override
  public void validate() {
    // Check if project is given in atleast one of the param or creds.
    if (StringUtils.isBlank(project) && !credentials.hasNonNull("project_id")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Validation failed. Project is required for GCP Cloud Monitoring");
    }

    // Use the project given in the creds if not given in the param.
    String project_id =
        StringUtils.isBlank(project) ? credentials.get("project_id").asText() : project;

    CloudResourceManager service;
    TestIamPermissionsRequest requestBody =
        new TestIamPermissionsRequest().setPermissions(REQUIRED_LOGGING_PERMISSIONS);

    try {
      // Try and initialise the GCP resource manager service.
      service = createCloudResourceManagerService(credentials);

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
