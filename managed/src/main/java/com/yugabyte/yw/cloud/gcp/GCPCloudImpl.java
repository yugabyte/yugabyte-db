// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud.gcp;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.googleapis.javanet.GoogleNetHttpTransport;
import com.google.api.client.http.HttpRequestInitializer;
import com.google.api.client.http.HttpTransport;
import com.google.api.client.json.gson.GsonFactory;
import com.google.api.services.compute.Compute;
import com.google.auth.http.HttpCredentialsAdapter;
import com.google.auth.oauth2.GoogleCredentials;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.io.ByteArrayInputStream;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang.StringUtils;

@Slf4j
public class GCPCloudImpl implements CloudAPI {

  public static final String PROJECT_ID_PROPERTY = "project_id";
  public static final String CLIENT_EMAIL_PROPERTY = "client_email";
  public static final String CUSTOM_GCE_NETWORK_PROPERTY = "CUSTOM_GCE_NETWORK";
  public static final String GCE_HOST_PROJECT_PROPERTY = "GCE_HOST_PROJECT";
  public static final String GCE_PROJECT_PROPERTY = "GCE_PROJECT";
  public static final String GCE_EMAIL_PROPERTY = "GCE_EMAIL";
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
  public boolean isValidCreds(Map<String, String> config, String region) {
    String projectId = config.get(PROJECT_ID_PROPERTY);
    if (StringUtils.isBlank(projectId)) {
      log.error("Project ID is not set, skipping validation");
      // TODO validate for service account.
      return true;
    }
    try {
      ObjectMapper mapper = new ObjectMapper();
      GoogleCredentials credentials =
          GoogleCredentials.fromStream(new ByteArrayInputStream(mapper.writeValueAsBytes(config)));
      HttpRequestInitializer requestInitializer = new HttpCredentialsAdapter(credentials);
      HttpTransport httpTransport = GoogleNetHttpTransport.newTrustedTransport();
      // Create Compute Engine object for listing instances.
      Compute compute =
          new Compute.Builder(httpTransport, GsonFactory.getDefaultInstance(), requestInitializer)
              .setApplicationName("")
              .build();
      compute.instances().aggregatedList(projectId).setMaxResults(1L).execute();

    } catch (Exception e) {
      log.error("Error in validating GCP credentials", e);
      return false;
    }
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }
}
