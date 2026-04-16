package com.yugabyte.yw.cloud.aws;

import static java.util.stream.Collectors.groupingBy;
import static java.util.stream.Collectors.mapping;
import static java.util.stream.Collectors.toSet;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentialsProvider;
import software.amazon.awssdk.auth.credentials.DefaultCredentialsProvider;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.awscore.exception.AwsServiceException;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.cloudtrail.CloudTrailClient;
import software.amazon.awssdk.services.ec2.Ec2Client;
import software.amazon.awssdk.services.ec2.model.AuthorizeSecurityGroupIngressRequest;
import software.amazon.awssdk.services.ec2.model.CancelCapacityReservationRequest;
import software.amazon.awssdk.services.ec2.model.CancelCapacityReservationResponse;
import software.amazon.awssdk.services.ec2.model.CreateCapacityReservationRequest;
import software.amazon.awssdk.services.ec2.model.CreateCapacityReservationResponse;
import software.amazon.awssdk.services.ec2.model.CreateKeyPairRequest;
import software.amazon.awssdk.services.ec2.model.CreateSecurityGroupRequest;
import software.amazon.awssdk.services.ec2.model.DeleteKeyPairRequest;
import software.amazon.awssdk.services.ec2.model.DescribeCapacityReservationsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeCapacityReservationsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeImagesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeImagesResponse;
import software.amazon.awssdk.services.ec2.model.DescribeInstanceTypeOfferingsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeInstanceTypeOfferingsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeInstanceTypesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeInstancesRequest;
import software.amazon.awssdk.services.ec2.model.DescribeKeyPairsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSecurityGroupsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSecurityGroupsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeSubnetsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeSubnetsResponse;
import software.amazon.awssdk.services.ec2.model.DescribeVpcsRequest;
import software.amazon.awssdk.services.ec2.model.DescribeVpcsResponse;
import software.amazon.awssdk.services.ec2.model.Ec2Exception;
import software.amazon.awssdk.services.ec2.model.Filter;
import software.amazon.awssdk.services.ec2.model.Image;
import software.amazon.awssdk.services.ec2.model.Instance;
import software.amazon.awssdk.services.ec2.model.InstanceTypeOffering;
import software.amazon.awssdk.services.ec2.model.LocationType;
import software.amazon.awssdk.services.ec2.model.Reservation;
import software.amazon.awssdk.services.ec2.model.ResourceType;
import software.amazon.awssdk.services.ec2.model.SecurityGroup;
import software.amazon.awssdk.services.ec2.model.Subnet;
import software.amazon.awssdk.services.ec2.model.TagSpecification;
import software.amazon.awssdk.services.ec2.model.Vpc;
import software.amazon.awssdk.services.elasticloadbalancingv2.ElasticLoadBalancingV2Client;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Action;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ActionTypeEnum;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateListenerRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.CreateTargetGroupRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DeregisterTargetsRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeListenersRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeLoadBalancersRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeTargetGroupsRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.DescribeTargetHealthRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ForwardActionConfig;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.InvalidConfigurationRequestException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Listener;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.LoadBalancer;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.Matcher;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyListenerRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyTargetGroupAttributesRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyTargetGroupAttributesResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyTargetGroupRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.ModifyTargetGroupResponse;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.RegisterTargetsRequest;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetDescription;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroup;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroupAttribute;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroupNotFoundException;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetGroupTuple;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetHealthDescription;
import software.amazon.awssdk.services.elasticloadbalancingv2.model.TargetTypeEnum;
import software.amazon.awssdk.services.kms.KmsClient;
import software.amazon.awssdk.services.kms.model.CreateKeyRequest;
import software.amazon.awssdk.services.kms.model.CreateKeyResponse;
import software.amazon.awssdk.services.kms.model.DisableKeyRequest;
import software.amazon.awssdk.services.kms.model.ScheduleKeyDeletionRequest;
import software.amazon.awssdk.services.kms.model.Tag;
import software.amazon.awssdk.services.route53.Route53Client;
import software.amazon.awssdk.services.route53.model.GetHostedZoneRequest;
import software.amazon.awssdk.services.route53.model.GetHostedZoneResponse;
import software.amazon.awssdk.services.sts.StsClient;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityRequest;
import software.amazon.awssdk.services.sts.model.GetCallerIdentityResponse;

// TODO - Better handling of UnauthorizedOperation. Ideally we should trigger alert so that
public class AWSCloudImpl implements CloudAPI {

  EncryptionAtRestManager keyManager;

  @Inject
  public AWSCloudImpl(EncryptionAtRestManager keyManager) {
    this.keyManager = keyManager;
  }

  public static final Logger LOG = LoggerFactory.getLogger(AWSCloudImpl.class);

  public ElasticLoadBalancingV2Client getELBClient(Provider provider, String regionCode) {
    AwsCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return ElasticLoadBalancingV2Client.builder()
        .region(software.amazon.awssdk.regions.Region.of(regionCode))
        .credentialsProvider(credentialsProvider)
        .build();
  }

  // TODO switch to async
  public Ec2Client getEC2Client(Provider provider, String regionCode) {
    AwsCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return Ec2Client.builder()
        .region(software.amazon.awssdk.regions.Region.of(regionCode))
        .credentialsProvider(credentialsProvider)
        .build();
  }

  public CloudTrailClient getCloudTrailClient(Provider provider, String region) {
    AwsCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return CloudTrailClient.builder()
        .region(software.amazon.awssdk.regions.Region.of(region))
        .credentialsProvider(credentialsProvider)
        .build();
  }

  public Route53Client getRoute53Client(Provider provider, String regionCode) {
    AwsCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return Route53Client.builder()
        .credentialsProvider(credentialsProvider)
        .region(software.amazon.awssdk.regions.Region.of(regionCode))
        .build();
  }

  public StsClient getStsClient(Provider provider, String regionCode) {
    AwsCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return StsClient.builder()
        .credentialsProvider(credentialsProvider)
        .region(software.amazon.awssdk.regions.Region.of(regionCode))
        .build();
  }

  private AwsCredentialsProvider getCredsOrFallbackToDefault(Provider provider) {
    String accessKeyId = provider.getDetails().getCloudInfo().getAws().awsAccessKeyID;
    String secretAccessKey = provider.getDetails().getCloudInfo().getAws().awsAccessKeySecret;
    if (checkKeysExists(provider)) {
      return StaticCredentialsProvider.create(
          AwsBasicCredentials.create(accessKeyId, secretAccessKey));
    } else {
      // If database creds do not exist we will fallback use default chain.
      return DefaultCredentialsProvider.create();
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
        Filter.builder().name("instance-type").values(instanceTypesFilter).build();
    // TODO: get rid of parallelStream in favour of async api using aws sdk 2.x
    List<DescribeInstanceTypeOfferingsResponse> results =
        azByRegionMap.entrySet().parallelStream()
            .map(
                regionAZListEntry -> {
                  Filter locationFilter =
                      Filter.builder()
                          .name("location")
                          .values(regionAZListEntry.getValue())
                          .build();
                  return getEC2Client(provider, regionAZListEntry.getKey().getCode())
                      .describeInstanceTypeOfferings(
                          DescribeInstanceTypeOfferingsRequest.builder()
                              .locationType(LocationType.AVAILABILITY_ZONE)
                              .filters(locationFilter, instanceTypeFilter)
                              .build());
                })
            .collect(Collectors.toList());

    return results.stream()
        .flatMap(result -> result.instanceTypeOfferings().stream())
        .collect(
            groupingBy(
                InstanceTypeOffering::instanceTypeAsString,
                mapping(InstanceTypeOffering::location, toSet())));
  }

  @Override
  public boolean isValidCreds(Provider provider) {
    // TODO: Remove this function once the validators are added for all cloud provider.
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    try {
      if (config.has(AwsKmsAuthConfigField.CMK_ID.fieldName)) {
        try {
          keyManager
              .getServiceInstance(KeyProvider.AWS.toString())
              .refreshKmsWithService(null, config);
          LOG.info("Validated AWS KMS creds for customer '{}'", customerUUID);
          return true;
        } catch (Exception e) {
          LOG.error("Cannot validate AWS KMS creds.", e);
          return false;
        }
      } else {
        KmsClient kmsClient = AwsEARServiceUtil.getKMSClient(null, config);
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
        String policyString = new ObjectMapper().writeValueAsString(keyPolicy);
        List<Tag> tags =
            Arrays.asList(
                Tag.builder().tagKey("customer-uuid").tagValue(customerUUID.toString()).build(),
                Tag.builder().tagKey("usage").tagValue("validate-aws-key-authenticity").build(),
                Tag.builder().tagKey("status").tagValue("deleted").build());
        CreateKeyRequest keyReq =
            CreateKeyRequest.builder()
                .description(keyDescription)
                .policy(policyString)
                .tags(tags)
                .build();
        CreateKeyResponse result = kmsClient.createKey(keyReq);
        // Disable and schedule the key for deletion. The minimum waiting period for
        // deletion is 7
        // days on AWS.
        String keyArn = result.keyMetadata().arn();
        DisableKeyRequest req = DisableKeyRequest.builder().keyId(keyArn).build();
        kmsClient.disableKey(req);
        ScheduleKeyDeletionRequest scheduleKeyDeletionRequest =
            ScheduleKeyDeletionRequest.builder().keyId(keyArn).pendingWindowInDays(7).build();
        kmsClient.scheduleKeyDeletion(scheduleKeyDeletionRequest);
        return true;
      }
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return false;
    }
  }

  // Load balancer methods
  private LoadBalancer getLoadBalancerByName(ElasticLoadBalancingV2Client lbClient, String lbName) {
    DescribeLoadBalancersRequest request =
        DescribeLoadBalancersRequest.builder().names(lbName).build();
    List<LoadBalancer> lbs = null;
    try {
      lbs = lbClient.describeLoadBalancers(request).loadBalancers();
      if (lbs.size() > 1) {
        throw new Exception("Failure: More than one load balancer with name \"" + lbName + "\"!");
      } else if (lbs.size() == 0) {
        throw new Exception("Failure: Load balancer with name \"" + lbName + "\" does not exist!");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return lbs.get(0);
  }

  // testing
  public LoadBalancer getLoadBalancerByName(Provider provider, String regionCode, String lbName) {
    ElasticLoadBalancingV2Client lbClient = getELBClient(provider, regionCode);
    return getLoadBalancerByName(lbClient, lbName);
  }

  private String getLoadBalancerArn(ElasticLoadBalancingV2Client lbClient, String lbName) {
    try {
      return getLoadBalancerByName(lbClient, lbName).loadBalancerArn();
    } catch (Exception e) {
      String message = "Error executing task {getLoadBalancerByArn()}, error='{}'";
      throw new RuntimeException(message, e);
    }
  }

  // Listener methods
  private Listener createListener(
      ElasticLoadBalancingV2Client lbClient,
      String lbName,
      String targetGroupArn,
      String protocol,
      int port) {
    String lbArn = getLoadBalancerArn(lbClient, lbName);
    TargetGroupTuple targetGroup =
        TargetGroupTuple.builder().targetGroupArn(targetGroupArn).build();
    ForwardActionConfig forwardConfig =
        ForwardActionConfig.builder().targetGroups(targetGroup).build();
    Action forwardToTargetGroup =
        Action.builder().type(ActionTypeEnum.FORWARD).forwardConfig(forwardConfig).build();
    CreateListenerRequest request =
        CreateListenerRequest.builder()
            .loadBalancerArn(lbArn)
            .protocol(protocol)
            .port(port)
            .defaultActions(forwardToTargetGroup)
            .build();
    Listener listener = lbClient.createListener(request).listeners().get(0);
    return listener;
  }

  private void setListenerTargetGroup(
      ElasticLoadBalancingV2Client lbClient, String listenerArn, String targetGroupArn) {
    TargetGroupTuple targetGroup =
        TargetGroupTuple.builder().targetGroupArn(targetGroupArn).build();
    ForwardActionConfig forwardConfig =
        ForwardActionConfig.builder().targetGroups(targetGroup).build();
    Action forwardToTargetGroup =
        Action.builder().type(ActionTypeEnum.FORWARD).forwardConfig(forwardConfig).build();
    ModifyListenerRequest request =
        ModifyListenerRequest.builder()
            .listenerArn(listenerArn)
            .defaultActions(forwardToTargetGroup)
            .build();
    lbClient.modifyListener(request);
  }

  private List<Listener> getListeners(ElasticLoadBalancingV2Client lbClient, String lbName) {
    String lbArn = getLoadBalancerArn(lbClient, lbName);
    DescribeListenersRequest request =
        DescribeListenersRequest.builder().loadBalancerArn(lbArn).build();
    List<Listener> listeners = lbClient.describeListeners(request).listeners();
    return listeners;
  }

  @VisibleForTesting
  Listener getListenerByPort(ElasticLoadBalancingV2Client lbClient, String lbName, int port) {
    List<Listener> listeners = getListeners(lbClient, lbName);
    for (Listener listener : listeners) {
      if (listener.port() == port) return listener;
    }
    return null;
  }

  // Manage load balancer node groups
  /**
   * Get all nodes registered to the provided target group.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param targetGroupArn the AWS target group arn.
   * @return a list of all nodes in the target group.
   */
  private List<TargetDescription> getTargetGroupNodes(
      ElasticLoadBalancingV2Client lbClient, String targetGroupArn) {
    // Get nodes in target group
    DescribeTargetHealthRequest request =
        DescribeTargetHealthRequest.builder().targetGroupArn(targetGroupArn).build();
    List<TargetHealthDescription> targetDescriptions =
        lbClient.describeTargetHealth(request).targetHealthDescriptions();
    List<TargetDescription> targets = new ArrayList<>();
    for (TargetHealthDescription targetDesc : targetDescriptions) {
      targets.add(targetDesc.target());
    }
    return targets;
  }

  /**
   * Check that the target group only contains the provided list of nodes with correct ports. If
   * missing, add them. If nodes not in the list are found in target group, remove them.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param targetGroupArn the target group arn.
   * @param instanceIDs list of EC2 node instance IDs.
   * @param port the port the target group nodes should be listening to.
   */
  private void checkTargetGroupNodes(
      ElasticLoadBalancingV2Client lbClient,
      String targetGroupArn,
      List<String> instanceIDs,
      int port) {
    if (CollectionUtils.isNotEmpty(instanceIDs)) {
      // Get nodes in target group
      List<TargetDescription> targets = getTargetGroupNodes(lbClient, targetGroupArn);
      // Get node instance IDs
      List<String> removeInstanceIDs = new ArrayList<>();
      List<String> currentInstanceIDs = new ArrayList<>();
      for (TargetDescription target : targets) {
        // Remove nodes with incorrect port
        if (target.port() != port) {
          removeInstanceIDs.add(target.id());
        } else {
          currentInstanceIDs.add(target.id());
        }
      }
      // Add/remove nodes from target group
      List<String> addInstanceIDs =
          instanceIDs.stream()
              .filter(i -> !currentInstanceIDs.contains(i))
              .collect(Collectors.toList());
      removeInstanceIDs.addAll(
          currentInstanceIDs.stream()
              .filter(i -> !instanceIDs.contains(i))
              .collect(Collectors.toList()));
      registerTargets(lbClient, targetGroupArn, addInstanceIDs, port);
      deregisterTargets(lbClient, targetGroupArn, removeInstanceIDs);
    } else {
      deregisterAllTargets(lbClient, targetGroupArn);
    }
  }

  /**
   * Add/remove DB nodes from the provided load balancer.
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param regionCode the region code.
   * @param lbName the load balancer name.
   * @param nodeIDs the DB node IDs (name, uuid).
   * @param protocol the listening protocol.
   * @param ports the listening ports enabled (YSQL, YCQL, YEDIS).
   */
  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      List<Integer> portsToForward,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    try {
      String lbProtocol = "TCP";
      // Get aws clients
      ElasticLoadBalancingV2Client lbClient = getELBClient(provider, regionCode);
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      // Get EC2 node instances
      List<NodeID> nodeIDs =
          azToNodeIDs.values().stream().flatMap(Collection::stream).collect(Collectors.toList());
      List<String> instanceIDs = getInstanceIDs(ec2Client, nodeIDs);
      // Check for listeners on each enabled port
      for (int port : portsToForward) {
        Listener listener = getListenerByPort(lbClient, lbName, port);
        // If no listener exists for a port, create target group and listener
        // else check target group settings and add/remove nodes from target group
        String targetGroupName = "tg-" + UUID.randomUUID().toString().substring(0, 29);
        String targetGroupArn = null;
        if (listener == null) {
          targetGroupArn =
              createNodeGroup(
                  lbClient,
                  lbName,
                  targetGroupName,
                  lbProtocol,
                  port,
                  instanceIDs,
                  healthCheckConfiguration);
          createListener(lbClient, lbName, targetGroupArn, lbProtocol, port);
        } else {
          // Check if listener has target group otherwise create one
          targetGroupArn = getListenerTargetGroup(listener);
          if (targetGroupArn == null) {
            targetGroupArn =
                createNodeGroup(
                    lbClient,
                    lbName,
                    targetGroupName,
                    lbProtocol,
                    port,
                    instanceIDs,
                    healthCheckConfiguration);
            setListenerTargetGroup(lbClient, listener.listenerArn(), targetGroupArn);
          } else {
            // Check node group
            checkNodeGroup(
                lbClient, targetGroupArn, lbProtocol, port, instanceIDs, healthCheckConfiguration);
          }
        }
        ensureTargetGroupAttributes(lbClient, targetGroupArn);
      }
    } catch (Exception e) {
      String message = "Error executing task {manageNodeGroup()}, error='{}'";
      throw new RuntimeException(message, e);
    }
  }

  /**
   * Check if the target group and the nodes inside the group have the correct protocol/port. Check
   * that the target group only contains the provided list of nodes.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param targetGroupArn the target group arn.
   * @param protocol the listening protocol.
   * @param port the listening port.
   * @param instanceIDs the EC2 node instance IDs.
   */
  @VisibleForTesting
  void checkNodeGroup(
      ElasticLoadBalancingV2Client lbClient,
      String targetGroupArn,
      String protocol,
      int port,
      List<String> instanceIDs,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    try {
      // Check target group settings
      TargetGroup targetGroup = getTargetGroup(lbClient, targetGroupArn);
      boolean validProtocol = targetGroup.protocol().equals(protocol);
      boolean validPort = targetGroup.port() == port;
      // If protocol or port incorrect then create new target group and update
      // listener
      if (!validProtocol || !validPort) {
        String targetGroupName = targetGroup.targetGroupName();
        throw new Exception(
            "Failure: Target Group \""
                + targetGroupName
                + "\" must have Protocol/Port = "
                + protocol
                + "/"
                + port);
      } else { // Check target group nodes
        checkTargetGroupNodes(lbClient, targetGroupArn, instanceIDs, port);
        checkTargetGroupHealthCheckConfiguration(
            lbClient, port, targetGroup, healthCheckConfiguration);
        // TODO: Check Heatch check for Target group
      }
    } catch (Exception e) {
      String message = "Error executing task {checkNodeGroup()}, error='{}'";
      throw new RuntimeException(message, e);
    }
  }

  private void checkTargetGroupHealthCheckConfiguration(
      ElasticLoadBalancingV2Client lbClient,
      int port,
      TargetGroup targetGroup,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    boolean healthCheckModified = false;
    ModifyTargetGroupRequest.Builder modifyTargetGroupRequestBuilder =
        ModifyTargetGroupRequest.builder().targetGroupArn(targetGroup.targetGroupArn());
    Protocol healthCheckProtocol = healthCheckConfiguration.getHealthCheckProtocol();
    List<Integer> healthCheckPorts = healthCheckConfiguration.getHealthCheckPorts();
    // If there is no health probe corrosponding to the port that is being forwareded, we
    // select the 0th indexed port as the default health check for that forwarding rule
    // This is because this case would only arise in case of custom health checks
    // TODO: Find a way to link the correct custom health check to the correct forwarding rule
    Integer healthCheckPort = healthCheckPorts.isEmpty() ? port : healthCheckPorts.get(0);
    String healthCheckPath =
        healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(healthCheckPort);
    if (!targetGroup.healthCheckProtocol().equals(healthCheckProtocol.name())) {
      modifyTargetGroupRequestBuilder =
          modifyTargetGroupRequestBuilder.healthCheckProtocol(healthCheckProtocol.name());
      healthCheckModified = true;
    }
    if (!targetGroup.healthCheckPort().equals(Integer.toString(healthCheckPort))) {
      modifyTargetGroupRequestBuilder =
          modifyTargetGroupRequestBuilder.healthCheckPort(Integer.toString(healthCheckPort));
      healthCheckModified = true;
    }
    if (healthCheckProtocol == Protocol.HTTP
        && (targetGroup.healthCheckPath() == null
            || !targetGroup.healthCheckPath().equals(healthCheckPath))) {
      modifyTargetGroupRequestBuilder =
          modifyTargetGroupRequestBuilder
              .healthCheckPath(healthCheckPath)
              .matcher(Matcher.builder().httpCode("200").build());
      healthCheckModified = true;
    }

    if (healthCheckModified) {
      try {
        ModifyTargetGroupRequest request = modifyTargetGroupRequestBuilder.build();
        ModifyTargetGroupResponse result = lbClient.modifyTargetGroup(request);
      } catch (TargetGroupNotFoundException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Target group not found: " + targetGroup.targetGroupArn());
      } catch (InvalidConfigurationRequestException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Invalid configuration request for target group: "
                + targetGroup.targetGroupArn()
                + " with attributes: "
                + modifyTargetGroupRequestBuilder.toString());
      } catch (Exception e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Error modifying target group: " + targetGroup.targetGroupArn() + " " + e.toString());
      }
    }
  }

  /**
   * Create a target group for the load balancer with the provided list of nodes.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param lbName the load balancer name.
   * @param targetGroupName the target group name.
   * @param protocol the listening protocol.
   * @param port the listening port.
   * @param instanceIDs the EC2 node instance IDs.
   * @return a string. The target group arn.
   */
  private String createNodeGroup(
      ElasticLoadBalancingV2Client lbClient,
      String lbName,
      String targetGroupName,
      String protocol,
      int port,
      List<String> instanceIDs,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    String vpc = getLoadBalancerByName(lbClient, lbName).vpcId();
    String targetGroupArn =
        createTargetGroup(lbClient, targetGroupName, protocol, port, vpc, healthCheckConfiguration);
    registerTargets(lbClient, targetGroupArn, instanceIDs, port);
    return targetGroupArn;
  }

  /**
   * Since by default target groups do not terminate connections when a node is deregistered, we
   * ensure that the default value is overriden to true.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param targetGroupArn the target group arn.
   */
  @VisibleForTesting
  void ensureTargetGroupAttributes(ElasticLoadBalancingV2Client lbClient, String targetGroupArn) {
    ModifyTargetGroupAttributesRequest request =
        ModifyTargetGroupAttributesRequest.builder()
            .targetGroupArn(targetGroupArn)
            .attributes(
                Arrays.asList(
                    TargetGroupAttribute.builder()
                        .key("deregistration_delay.connection_termination.enabled")
                        .value("true")
                        .build()))
            .build();
    try {
      ModifyTargetGroupAttributesResponse result = lbClient.modifyTargetGroupAttributes(request);
    } catch (TargetGroupNotFoundException e) {
      LOG.warn("No such target group with targetGroupArn: " + request.targetGroupArn());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Target group not found: " + request.targetGroupArn());
    } catch (InvalidConfigurationRequestException e) {
      LOG.warn(
          "Attempt to set invalid configuration on target group with targetGroupArn: "
              + request.targetGroupArn());
      LOG.info("Target group attributes: " + request.attributes().toString());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to update attributes of target group.");
    }
  }

  // Target group methods
  private TargetGroup getTargetGroup(ElasticLoadBalancingV2Client lbClient, String targetGroupArn) {
    DescribeTargetGroupsRequest request =
        DescribeTargetGroupsRequest.builder().targetGroupArns(targetGroupArn).build();
    return lbClient.describeTargetGroups(request).targetGroups().get(0);
  }

  @VisibleForTesting
  String getListenerTargetGroup(Listener listener) {
    List<Action> actions = listener.defaultActions();
    for (Action action : actions) {
      if (action.type().equals(ActionTypeEnum.FORWARD.toString())) {

        return action.targetGroupArn();
      }
    }
    return null;
  }

  private String createTargetGroup(
      ElasticLoadBalancingV2Client lbClient,
      String name,
      String protocol,
      int port,
      String vpc,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    CreateTargetGroupRequest.Builder targetGroupRequestBuilder =
        CreateTargetGroupRequest.builder()
            .name(name)
            .protocol(protocol)
            .port(port)
            .vpcId(vpc)
            .targetType(TargetTypeEnum.INSTANCE)
            .healthCheckProtocol(healthCheckConfiguration.getHealthCheckProtocol().name())
            .healthCheckPort(Integer.toString(port));
    if (healthCheckConfiguration.getHealthCheckProtocol().equals(Protocol.HTTP)) {
      targetGroupRequestBuilder =
          targetGroupRequestBuilder
              .healthCheckPath(healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(port))
              .matcher(Matcher.builder().httpCode("200").build());
    }
    CreateTargetGroupRequest targetGroupRequest = targetGroupRequestBuilder.build();
    TargetGroup targetGroup = lbClient.createTargetGroup(targetGroupRequest).targetGroups().get(0);
    String targetGroupArn = targetGroup.targetGroupArn();
    return targetGroupArn;
  }

  private void registerTargets(
      ElasticLoadBalancingV2Client lbClient,
      String targetGroupArn,
      List<String> instanceIDs,
      int port) {
    if (CollectionUtils.isNotEmpty(instanceIDs)) {
      List<TargetDescription> targets = new ArrayList<>();
      for (String id : instanceIDs) {
        TargetDescription target = TargetDescription.builder().id(id).port(port).build();
        targets.add(target);
      }
      RegisterTargetsRequest request =
          RegisterTargetsRequest.builder().targetGroupArn(targetGroupArn).targets(targets).build();
      lbClient.registerTargets(request);
    }
  }

  /**
   * Returns the list of target objects holding the node instances.
   *
   * @param lbClient the AWS ELB client for API calls.
   * @param targetGroupArn the target group arn.
   * @param instanceIDs the EC2 node instance IDs.
   * @return a list of target objects representing the node instances.
   */
  private List<TargetDescription> getTargets(
      ElasticLoadBalancingV2Client lbClient, String targetGroupArn, List<String> instanceIDs) {
    List<TargetDescription> allTargets = getTargetGroupNodes(lbClient, targetGroupArn);
    List<TargetDescription> targets =
        allTargets.stream().filter(t -> instanceIDs.contains(t.id())).collect(Collectors.toList());
    return targets;
  }

  private void deregisterTargets(
      ElasticLoadBalancingV2Client lbClient, String targetGroupArn, List<String> instanceIDs) {
    if (CollectionUtils.isNotEmpty(instanceIDs)) {
      List<TargetDescription> targets = getTargets(lbClient, targetGroupArn, instanceIDs);
      DeregisterTargetsRequest request =
          DeregisterTargetsRequest.builder()
              .targetGroupArn(targetGroupArn)
              .targets(targets)
              .build();
      lbClient.deregisterTargets(request);
    }
  }

  private void deregisterAllTargets(ElasticLoadBalancingV2Client lbClient, String targetGroupArn) {
    List<TargetDescription> targets = getTargetGroupNodes(lbClient, targetGroupArn);
    if (CollectionUtils.isNotEmpty(targets)) {
      DeregisterTargetsRequest request =
          DeregisterTargetsRequest.builder()
              .targetGroupArn(targetGroupArn)
              .targets(targets)
              .build();
      lbClient.deregisterTargets(request);
    }
  }

  // Helper methods
  /**
   * Returns the EC2 node instance IDs given the node name/uuid. Filtering by node name and then
   * uuid.
   *
   * @param ec2Client the AWS EC2 client for API calls.
   * @param nodeIDs the node IDs (name, uuid).
   * @return a list. The node instance IDs.
   */
  @VisibleForTesting
  List<String> getInstanceIDs(Ec2Client ec2Client, List<NodeID> nodeIDs) {
    if (CollectionUtils.isEmpty(nodeIDs)) {
      return new ArrayList<>();
    }
    List<String> nodeNames =
        nodeIDs.stream().map(nodeId -> nodeId.getName()).collect(Collectors.toList());
    // Get instances by node name
    Filter filterName = Filter.builder().name("tag:Name").values(nodeNames).build();
    List<String> states = ImmutableList.of("pending", "running", "stopping", "stopped");
    Filter filterState = Filter.builder().name("instance-state-name").values(states).build();
    DescribeInstancesRequest instanceRequest =
        DescribeInstancesRequest.builder().filters(filterName, filterState).build();
    List<Reservation> reservations = ec2Client.describeInstances(instanceRequest).reservations();
    // Filter by matching nodeUUIDs and older nodes missing UUID
    Map<NodeID, List<String>> nodeToInstances = new HashMap<>();
    for (Reservation r : reservations) {
      for (Instance i : r.instances()) {
        nodeToInstances.computeIfAbsent(getNodeIDs(i), k -> new ArrayList<>()).add(i.instanceId());
      }
    }

    // Filter once more against our original node set
    List<String> instanceIDs = new ArrayList<>();
    for (NodeID id : nodeIDs) {
      List<String> ids = nodeToInstances.getOrDefault(id, Collections.emptyList());
      if (ids.isEmpty()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Failure: node instance with name \"" + id.getName() + "\" not found");
      } else if (ids.size() > 1) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Failure: multiple nodes with name \"" + id.getName() + "\" and no UUID are found");
      }
      instanceIDs.addAll(ids);
    }
    return instanceIDs;
  }

  private NodeID getNodeIDs(Instance instance) {
    String name = null;
    String uuid = null;
    for (software.amazon.awssdk.services.ec2.model.Tag tag : instance.tags()) {
      if (tag.key().equals("Name")) name = tag.value();
      if (tag.key().equals("node-uuid")) uuid = tag.value();
    }
    return new NodeID(name, uuid);
  }

  public GetCallerIdentityResponse getStsClientOrBadRequest(Provider provider, Region region) {
    try {
      StsClient stsClient = getStsClient(provider, region.getCode());
      return stsClient.getCallerIdentity(GetCallerIdentityRequest.builder().build());
    } catch (AwsServiceException | SdkClientException e) {
      LOG.error("AWS Provider validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "AWS access and secret keys validation failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeInstanceOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeInstancesRequest request = DescribeInstancesRequest.builder().dryRun(true).build();
      try {
        // If the dry run is successful, the error response is DryRunOperation
        ec2Client.describeInstances(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider validation dry run failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeInstances failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeImageOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeImagesRequest request = DescribeImagesRequest.builder().dryRun(true).build();
      try {
        // If the dry run is successful, the error response is DryRunOperation
        ec2Client.describeImages(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider image dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeImages failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeInstanceTypesOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeInstanceTypesRequest request =
          DescribeInstanceTypesRequest.builder().dryRun(true).build();
      try {
        // If the dry run is successful, the error response is DryRunOperation
        ec2Client.describeInstanceTypes(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider instance types dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeInstanceTypes failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeVpcsOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeVpcsRequest request = DescribeVpcsRequest.builder().dryRun(true).build();
      try {
        // If the dry run is successful, the error response is DryRunOperation
        ec2Client.describeVpcs(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider vpc dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeVpcs failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeSubnetOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeSubnetsRequest request = DescribeSubnetsRequest.builder().dryRun(true).build();
      try {
        // If dry run is successful, the error response is DryRunOperation
        ec2Client.describeSubnets(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider Subnet dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeSubnets failed: " + e.getMessage());
    }
  }

  public boolean dryRunSecurityGroupOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeSecurityGroupsRequest describeRequest =
          DescribeSecurityGroupsRequest.builder().dryRun(true).build();

      CreateSecurityGroupRequest createRequest =
          CreateSecurityGroupRequest.builder().dryRun(true).build();

      // Attempt dry-run of describeSecurityGroups and createSecurityGroup
      boolean dryRunSucceeded = false;
      String message = "";
      try {
        ec2Client.describeSecurityGroups(describeRequest);
      } catch (Ec2Exception e) {
        message = e.getMessage();
        if ("DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          dryRunSucceeded = true;
        }
      }
      try {
        ec2Client.createSecurityGroup(createRequest);
      } catch (Ec2Exception e) {
        if ("DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          dryRunSucceeded = true;
        }
      }
      if (!dryRunSucceeded) {
        throw new PlatformServiceException(BAD_REQUEST, message);
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider SecurityGroup dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS SecurityGroup failed: " + e.getMessage());
    }
  }

  public boolean dryRunKeyPairOrBadRequest(Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      DescribeKeyPairsRequest describeRequest =
          DescribeKeyPairsRequest.builder().dryRun(true).build();

      CreateKeyPairRequest createRequest = CreateKeyPairRequest.builder().dryRun(true).build();

      boolean dryRunSucceeded = false;
      String message = "";

      // Attempt dry-run of describeKeyPairs and createKeyPair
      try {
        ec2Client.describeKeyPairs(describeRequest);
      } catch (Ec2Exception e) {
        if ("DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          dryRunSucceeded = true;
        }
        message = e.getMessage();
      }
      try {
        ec2Client.createKeyPair(createRequest);
      } catch (Ec2Exception e) {
        if ("DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          dryRunSucceeded = true;
        }
      }

      if (!dryRunSucceeded) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Dry run of AWS KeyPair failed: " + message);
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider KeyPair dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS KeyPair failed: " + e.getMessage());
    }
  }

  public boolean dryRunAuthorizeSecurityGroupIngressOrBadRequest(
      Provider provider, String regionCode) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);
      AuthorizeSecurityGroupIngressRequest request =
          AuthorizeSecurityGroupIngressRequest.builder().dryRun(true).build();
      try {
        ec2Client.authorizeSecurityGroupIngress(request);
      } catch (Ec2Exception e) {
        if (!"DryRunOperation".equals(e.awsErrorDetails().errorCode())) {
          throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
        }
      }
      return true;
    } catch (AwsServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider AuthorizeSecurityGroupIngress dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS AuthorizeSecurityGroupIngress failed: " + e.getMessage());
    }
  }

  public GetHostedZoneResponse getHostedZoneOrBadRequest(
      Provider provider, Region region, String hostedZoneId) {
    try {
      Route53Client route53Client = getRoute53Client(provider, region.getCode());
      GetHostedZoneRequest request = GetHostedZoneRequest.builder().id(hostedZoneId).build();
      return route53Client.getHostedZone(request);
    } catch (AwsServiceException e) {
      LOG.error("Hosted Zone validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Hosted Zone validation failed: " + e.getMessage());
    }
  }

  public Image describeImageOrBadRequest(Provider provider, Region region, String imageId) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, region.getCode());
      DescribeImagesRequest request = DescribeImagesRequest.builder().imageIds(imageId).build();
      DescribeImagesResponse result = ec2Client.describeImages(request);
      return result.images().get(0);
    } catch (AwsServiceException e) {
      LOG.error("AMI details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "AMI details extraction failed: " + e.getMessage());
    }
  }

  public List<SecurityGroup> describeSecurityGroupsOrBadRequest(Provider provider, Region region) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, region.getCode());
      DescribeSecurityGroupsRequest request =
          DescribeSecurityGroupsRequest.builder()
              .groupIds(Arrays.asList(region.getSecurityGroupId().split("\\s*,\\s*")))
              .build();
      DescribeSecurityGroupsResponse result = ec2Client.describeSecurityGroups(request);
      return result.securityGroups();
    } catch (AwsServiceException e) {
      LOG.error("Security group details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Security group extraction failed: " + e.getMessage());
    }
  }

  public Vpc describeVpcOrBadRequest(Provider provider, Region region) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, region.getCode());
      DescribeVpcsRequest request =
          DescribeVpcsRequest.builder().vpcIds(region.getVnetName()).build();
      DescribeVpcsResponse result = ec2Client.describeVpcs(request);
      return result.vpcs().get(0);
    } catch (AwsServiceException e) {
      LOG.error("Vpc details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Vpc details extraction failed: " + e.getMessage());
    }
  }

  public List<Subnet> describeSubnetsOrBadRequest(Provider provider, Region region) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, region.getCode());
      DescribeSubnetsRequest request =
          DescribeSubnetsRequest.builder()
              .subnetIds(
                  region.getZones().stream()
                      .map(zone -> zone.getSubnet())
                      .collect(Collectors.toList()))
              .build();
      DescribeSubnetsResponse result = ec2Client.describeSubnets(request);
      return result.subnets();
    } catch (AwsServiceException e) {
      LOG.error("Subnet details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Subnet details extraction failed: " + e.getMessage());
    }
  }

  public boolean checkKeysExists(Provider provider) {
    AWSCloudInfo cloudInfo = provider.getDetails().getCloudInfo().getAws();
    return !StringUtils.isEmpty(cloudInfo.awsAccessKeyID)
        && !StringUtils.isEmpty(cloudInfo.awsAccessKeySecret);
  }

  public void deleteKeyPair(Provider provider, Region region, String keyPairName) {
    List<Region> regions = new ArrayList<Region>();
    regions.add(region);
    if (regions.size() == 0) {
      regions = provider.getRegions();
    }

    try {
      for (Region r : regions) {
        Ec2Client ec2Client = getEC2Client(provider, r.getCode());
        DeleteKeyPairRequest request = DeleteKeyPairRequest.builder().keyName(keyPairName).build();
        ec2Client.deleteKeyPair(request);
      }
    } catch (AwsServiceException e) {
      LOG.error("Access Key deletion failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Access Key deletion failed: " + e.getMessage());
    }
  }

  /**
   * Creates a capacity reservation in AWS (idempotent) Returns existing reservation ID if one with
   * the same name already exists and is active.
   *
   * @param provider the cloud provider bean for the AWS provider
   * @param reservationName Name of the reservation
   * @param regionCode AWS region code (e.g., "us-east-1")
   * @param availabilityZone AWS availability zone (e.g., "us-east-1a")
   * @param instanceType Instance type (e.g., "m5.large")
   * @param count Number of instances to reserve
   * @param tags The tags that need to be associated with the capacity reservation
   * @return The capacity reservation ID
   */
  public String createCapacityReservation(
      Provider provider,
      String reservationName,
      String regionCode,
      String availabilityZone,
      String instanceType,
      int count,
      Map<String, String> tags) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);

      // Check if a capacity reservation with the same name already exists
      String existingReservationId = findExistingCapacityReservation(ec2Client, reservationName);

      if (existingReservationId != null) {
        LOG.info(
            "Capacity reservation already exists: {} (ID: {})",
            reservationName,
            existingReservationId);
        return existingReservationId;
      }

      List<software.amazon.awssdk.services.ec2.model.Tag> tagList = new ArrayList<>();
      tagList.add(
          software.amazon.awssdk.services.ec2.model.Tag.builder()
              .key("Name")
              .value(reservationName)
              .build());

      // Add all tags from the map
      if (tags != null && !tags.isEmpty()) {
        tags.forEach(
            (key, value) ->
                tagList.add(
                    software.amazon.awssdk.services.ec2.model.Tag.builder()
                        .key(key)
                        .value(value)
                        .build()));
      }

      // Create new reservation
      CreateCapacityReservationRequest request =
          CreateCapacityReservationRequest.builder()
              .instanceType(instanceType)
              .instancePlatform("Linux/UNIX")
              .availabilityZone(availabilityZone)
              .instanceCount(count)
              .endDateType("unlimited")
              .instanceMatchCriteria("targeted")
              .tagSpecifications(
                  TagSpecification.builder()
                      .resourceType(ResourceType.CAPACITY_RESERVATION)
                      .tags(tagList)
                      .build())
              .build();

      LOG.debug(
          "Creating capacity reservation: {} in availability zone: {} for {} {} instances",
          reservationName,
          availabilityZone,
          count,
          instanceType);

      CreateCapacityReservationResponse result = ec2Client.createCapacityReservation(request);
      String capacityReservationId = result.capacityReservation().capacityReservationId();

      LOG.info(
          "Successfully created capacity reservation: {} (ID: {}) in availability zone: {} with {}"
              + " {} instances",
          reservationName,
          capacityReservationId,
          availabilityZone,
          count,
          instanceType);

      return capacityReservationId;

    } catch (AwsServiceException e) {
      LOG.error("Capacity reservation creation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Capacity reservation creation failed: " + e.getMessage());
    }
  }

  /**
   * Finds an existing active capacity reservation by name
   *
   * @param ec2Client EC2 client
   * @param reservationName Name of the reservation to search for
   * @return Capacity reservation ID if found, null otherwise
   */
  private String findExistingCapacityReservation(Ec2Client ec2Client, String reservationName) {
    try {
      DescribeCapacityReservationsRequest describeRequest =
          DescribeCapacityReservationsRequest.builder()
              .filters(
                  Filter.builder().name("tag:Name").values(reservationName).build(),
                  Filter.builder().name("state").values("active").build())
              .build();

      DescribeCapacityReservationsResponse describeResult =
          ec2Client.describeCapacityReservations(describeRequest);

      if (!describeResult.capacityReservations().isEmpty()) {
        String reservationId = describeResult.capacityReservations().get(0).capacityReservationId();
        LOG.debug(
            "Found existing capacity reservation: {} (ID: {})", reservationName, reservationId);
        return reservationId;
      }

      return null; // No matching reservation found

    } catch (AwsServiceException e) {
      LOG.warn("Failed to check for existing capacity reservations: {}", e.getMessage());
      // Return null to proceed with creation attempt
      return null;
    }
  }

  /**
   * Deletes a capacity reservation from AWS
   *
   * @param provider the cloud provider bean for the AWS provider
   * @param regionCode AWS region code where the reservation exists
   * @param capacityReservationId The capacity reservation ID to delete
   */
  public void deleteCapacityReservation(
      Provider provider, String regionCode, String capacityReservationId) {
    try {
      Ec2Client ec2Client = getEC2Client(provider, regionCode);

      CancelCapacityReservationRequest request =
          CancelCapacityReservationRequest.builder()
              .capacityReservationId(capacityReservationId)
              .build();

      LOG.debug(
          "Deleting capacity reservation with ID: {} in region: {}",
          capacityReservationId,
          regionCode);

      CancelCapacityReservationResponse result = ec2Client.cancelCapacityReservation(request);

      if (result.returnValue()) {
        LOG.info(
            "Successfully deleted capacity reservation with ID: {} in region: {}",
            capacityReservationId,
            regionCode);
      } else {
        LOG.warn(
            "Capacity reservation deletion may have failed for ID: {} in region: {}",
            capacityReservationId,
            regionCode);
      }

    } catch (AwsServiceException e) {
      LOG.error("Capacity reservation deletion failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Capacity reservation deletion failed: " + e.getMessage());
    }
  }
}
