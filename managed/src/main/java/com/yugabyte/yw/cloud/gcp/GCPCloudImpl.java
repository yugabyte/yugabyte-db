// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud.gcp;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.googleapis.json.GoogleJsonResponseException;
import com.google.api.client.http.HttpRequestInitializer;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.services.compute.Compute;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NodeID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

import org.apache.commons.lang3.StringUtils;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.security.GeneralSecurityException;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;

@Slf4j
public class GCPCloudImpl implements CloudAPI {

  public static final String PROJECT_ID_PROPERTY = "gce_project";
  public static final String CUSTOM_GCE_NETWORK_PROPERTY = "CUSTOM_GCE_NETWORK";
  public static final String GCE_PROJECT_PROPERTY = "GCE_PROJECT";
  public static final String GOOGLE_APPLICATION_CREDENTIALS_PROPERTY =
      "GOOGLE_APPLICATION_CREDENTIALS";

  /**
   * Find the instance types offered in availabilityZones.
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param azByRegionMap user selected availabilityZones by their parent region.
   * @param instanceTypesFilter list of instanceTypes for which we want to list the offerings.
   * @return a map. Key of this map is instance type like "c5.xlarge" and value is all the
   *     availabilityZones for which the instance type is being offered.
   */
  @Override
  public Map<String, Set<String>> offeredZonesByInstanceType(
      Provider provider, Map<Region, Set<String>> azByRegionMap, Set<String> instanceTypesFilter) {
    // TODO make a call to the cloud provider to populate.
    // Make the instances available in all availabilityZones.
    Set<String> azs =
        azByRegionMap.values().stream().flatMap(s -> s.stream()).collect(Collectors.toSet());
    return instanceTypesFilter.stream().collect(Collectors.toMap(Function.identity(), i -> azs));
  }

  // Basic validation to make sure that the credentials work with GCP.
  @Override
  public boolean isValidCreds(Provider provider, String region) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    String projectId = gcpCloudInfo.getGceProject();
    if (StringUtils.isBlank(projectId)) {
      log.error("Project ID is not set, skipping validation");
      // TODO validate for service account.
      return true;
    }
    try {
      Compute compute = buildComputeClient(gcpCloudInfo);
      compute.instances().aggregatedList(projectId).setMaxResults(1L).execute();
    } catch (GeneralSecurityException | IOException e) {
      log.error("Error in validating GCP credentials", e);
      return false;
    }
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }

  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      List<String> nodeNames,
      List<NodeID> nodeIDs,
      String protocol,
      List<Integer> ports) {}

  @Override
  public void validateInstanceTemplate(Provider provider, String instanceTemplate) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    String projectId = gcpCloudInfo.getGceProject();

    if (StringUtils.isBlank(projectId)) {
      String errorMessage = "Project ID must be set for instance template validation";
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }
    String errorMessage = "Unable to validate GCP instance template: " + instanceTemplate;
    try {
      Compute compute = buildComputeClient(gcpCloudInfo);
      compute.instanceTemplates().get(projectId, instanceTemplate).execute();
    } catch (GeneralSecurityException e) {
      log.error("GeneralSecurityException validating instance template", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errorMessage);
    } catch (GoogleJsonResponseException e) {
      log.error("GoogleJsonResponseException validating instance template", e);
      throw new PlatformServiceException(e.getStatusCode(), e.getMessage());
    } catch (IOException e) {
      log.error("IOException validating instance template", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errorMessage);
    }
  }

  private Compute buildComputeClient(GCPCloudInfo cloudInfo)
      throws GeneralSecurityException, IOException {
    ObjectMapper mapper = Json.mapper();
    JsonNode gcpCredentials = cloudInfo.getGceApplicationCredentials();
    GoogleCredentials credentials =
        GoogleCredentials.fromStream(
            new ByteArrayInputStream(mapper.writeValueAsBytes(gcpCredentials)));
    HttpRequestInitializer requestInitializer = new HttpCredentialsAdapter(credentials);
    HttpTransport httpTransport = GoogleNetHttpTransport.newTrustedTransport();
    // Create Compute Engine object.
    return new Compute.Builder(httpTransport, GsonFactory.getDefaultInstance(), requestInitializer)
        .setApplicationName("")
        .build();
  }
}
