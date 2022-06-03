package com.yugabyte.yw.cloud.aws;

import static java.util.stream.Collectors.groupingBy;
import static java.util.stream.Collectors.mapping;
import static java.util.stream.Collectors.toSet;

import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.ec2.AmazonEC2;
import com.amazonaws.services.ec2.AmazonEC2ClientBuilder;
import com.amazonaws.services.ec2.model.DescribeInstanceTypeOfferingsRequest;
import com.amazonaws.services.ec2.model.DescribeInstanceTypeOfferingsResult;
import com.amazonaws.services.ec2.model.DescribeInstancesRequest;
import com.amazonaws.services.ec2.model.DryRunResult;
import com.amazonaws.services.ec2.model.Filter;
import com.amazonaws.services.ec2.model.InstanceTypeOffering;
import com.amazonaws.services.ec2.model.LocationType;
import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DisableKeyRequest;
import com.amazonaws.services.kms.model.ScheduleKeyDeletionRequest;
import com.amazonaws.services.kms.model.Tag;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

// TODO - Better handling of UnauthorizedOperation. Ideally we should trigger alert so that
// site admin knows about it
public class AWSCloudImpl implements CloudAPI {
  public static final Logger LOG = LoggerFactory.getLogger(AWSCloudImpl.class);

  // TODO use aws sdk 2.x and switch to async
  public AmazonEC2 getEC2Client(Provider provider, Region r) {
    return getEC2ClientInternal(provider.getUnmaskedConfig(), r.code);
  }

  private AmazonEC2 getEC2ClientInternal(Map<String, String> config, String regionCode) {
    AWSCredentialsProvider credentialsProvider =
        getCredsOrFallbackToDefault(
            config.get("AWS_ACCESS_KEY_ID"), config.get("AWS_SECRET_ACCESS_KEY"));
    return AmazonEC2ClientBuilder.standard()
        .withRegion(regionCode)
        .withCredentials(credentialsProvider)
        .build();
  }

  // TODO: move to some common utils
  private static AWSCredentialsProvider getCredsOrFallbackToDefault(
      String accessKeyId, String secretAccessKey) {
    if (!Strings.isNullOrEmpty(accessKeyId) && !Strings.isNullOrEmpty(secretAccessKey)) {
      return new AWSStaticCredentialsProvider(
          new BasicAWSCredentials(accessKeyId, secretAccessKey));
    } else {

      // If database creds do not exist we will fallback use default chain.
      return new DefaultAWSCredentialsProviderChain();
    }
  }

  /**
   * Make describe instance offerings calls for all the regions in azByRegionMap.keySet(). Use
   * supplied instanceTypesFilter and availabilityZones (azByRegionMap) as filter for this describe
   * call.
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
    Filter instanceTypeFilter =
        new Filter().withName("instance-type").withValues(instanceTypesFilter);
    // TODO: get rid of parallelStream in favour of async api using aws sdk 2.x
    List<DescribeInstanceTypeOfferingsResult> results =
        azByRegionMap
            .entrySet()
            .parallelStream()
            .map(
                regionAZListEntry -> {
                  Filter locationFilter =
                      new Filter().withName("location").withValues(regionAZListEntry.getValue());
                  return getEC2Client(provider, regionAZListEntry.getKey())
                      .describeInstanceTypeOfferings(
                          new DescribeInstanceTypeOfferingsRequest()
                              .withLocationType(LocationType.AvailabilityZone)
                              .withFilters(locationFilter, instanceTypeFilter));
                })
            .collect(Collectors.toList());

    return results
        .stream()
        .flatMap(result -> result.getInstanceTypeOfferings().stream())
        .collect(
            groupingBy(
                InstanceTypeOffering::getInstanceType,
                mapping(InstanceTypeOffering::getLocation, toSet())));
  }

  @Override
  public boolean isValidCreds(Map<String, String> config, String region) {
    try {
      AmazonEC2 ec2Client = getEC2ClientInternal(config, region);
      DryRunResult<DescribeInstancesRequest> dryRunResult =
          ec2Client.dryRun(new DescribeInstancesRequest());
      if (!dryRunResult.isSuccessful()) {
        LOG.error(dryRunResult.getDryRunResponse().getMessage());
        return false;
      }
      return dryRunResult.isSuccessful();
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return false;
    }
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    try {
      AWSKMS kmsClient = AwsEARServiceUtil.getKMSClient(null, config);
      // Create a key.
      String keyDescription =
          "Fake key to test the authenticity of the credentials. It is scheduled to be deleted. "
              + "DO NOT USE.";
      ObjectNode keyPolicy = Json.newObject().put("Version", "2012-10-17");
      ObjectNode keyPolicyStatement = Json.newObject();
      keyPolicyStatement.put("Effect", "Allow");
      keyPolicyStatement.put("Resource", "*");
      ArrayNode keyPolicyActions =
          Json.newArray()
              .add("kms:Create*")
              .add("kms:Put*")
              .add("kms:DisableKey")
              .add("kms:ScheduleKeyDeletion");
      keyPolicyStatement.set("Principal", Json.newObject().put("AWS", "*"));
      keyPolicyStatement.set("Action", keyPolicyActions);
      keyPolicy.set("Statement", Json.newArray().add(keyPolicyStatement));
      CreateKeyRequest keyReq =
          new CreateKeyRequest()
              .withDescription(keyDescription)
              .withPolicy(new ObjectMapper().writeValueAsString(keyPolicy))
              .withTags(
                  new Tag().withTagKey("customer-uuid").withTagValue(customerUUID.toString()),
                  new Tag().withTagKey("usage").withTagValue("validate-aws-key-authenticity"),
                  new Tag().withTagKey("status").withTagValue("deleted"));
      CreateKeyResult result = kmsClient.createKey(keyReq);
      // Disable and schedule the key for deletion. The minimum waiting period for deletion is 7
      // days on AWS.
      String keyArn = result.getKeyMetadata().getArn();
      DisableKeyRequest req = new DisableKeyRequest().withKeyId(keyArn);
      kmsClient.disableKey(req);
      ScheduleKeyDeletionRequest scheduleKeyDeletionRequest =
          new ScheduleKeyDeletionRequest().withKeyId(keyArn).withPendingWindowInDays(7);
      kmsClient.scheduleKeyDeletion(scheduleKeyDeletionRequest);
      return true;
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return false;
    }
  }
}
