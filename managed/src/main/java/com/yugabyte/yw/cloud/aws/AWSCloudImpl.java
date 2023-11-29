package com.yugabyte.yw.cloud.aws;

import static java.util.stream.Collectors.groupingBy;
import static java.util.stream.Collectors.mapping;
import static java.util.stream.Collectors.toSet;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.amazonaws.AmazonServiceException;
import com.amazonaws.SdkClientException;
import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.cloudtrail.AWSCloudTrail;
import com.amazonaws.services.cloudtrail.AWSCloudTrailClientBuilder;
import com.amazonaws.services.ec2.AmazonEC2;
import com.amazonaws.services.ec2.AmazonEC2ClientBuilder;
import com.amazonaws.services.ec2.model.AuthorizeSecurityGroupIngressRequest;
import com.amazonaws.services.ec2.model.CreateKeyPairRequest;
import com.amazonaws.services.ec2.model.CreateSecurityGroupRequest;
import com.amazonaws.services.ec2.model.DeleteKeyPairRequest;
import com.amazonaws.services.ec2.model.DescribeImagesRequest;
import com.amazonaws.services.ec2.model.DescribeImagesResult;
import com.amazonaws.services.ec2.model.DescribeInstanceTypeOfferingsRequest;
import com.amazonaws.services.ec2.model.DescribeInstanceTypeOfferingsResult;
import com.amazonaws.services.ec2.model.DescribeInstanceTypesRequest;
import com.amazonaws.services.ec2.model.DescribeInstancesRequest;
import com.amazonaws.services.ec2.model.DescribeKeyPairsRequest;
import com.amazonaws.services.ec2.model.DescribeSecurityGroupsRequest;
import com.amazonaws.services.ec2.model.DescribeSecurityGroupsResult;
import com.amazonaws.services.ec2.model.DescribeSubnetsRequest;
import com.amazonaws.services.ec2.model.DescribeSubnetsResult;
import com.amazonaws.services.ec2.model.DescribeVpcsRequest;
import com.amazonaws.services.ec2.model.DescribeVpcsResult;
import com.amazonaws.services.ec2.model.DryRunResult;
import com.amazonaws.services.ec2.model.Filter;
import com.amazonaws.services.ec2.model.Image;
import com.amazonaws.services.ec2.model.Instance;
import com.amazonaws.services.ec2.model.InstanceTypeOffering;
import com.amazonaws.services.ec2.model.LocationType;
import com.amazonaws.services.ec2.model.Reservation;
import com.amazonaws.services.ec2.model.SecurityGroup;
import com.amazonaws.services.ec2.model.Subnet;
import com.amazonaws.services.ec2.model.Vpc;
import com.amazonaws.services.elasticloadbalancingv2.AmazonElasticLoadBalancing;
import com.amazonaws.services.elasticloadbalancingv2.AmazonElasticLoadBalancingClientBuilder;
import com.amazonaws.services.elasticloadbalancingv2.model.Action;
import com.amazonaws.services.elasticloadbalancingv2.model.ActionTypeEnum;
import com.amazonaws.services.elasticloadbalancingv2.model.CreateListenerRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.CreateTargetGroupRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.DeregisterTargetsRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.DescribeListenersRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.DescribeLoadBalancersRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.DescribeTargetGroupsRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.DescribeTargetHealthRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.ForwardActionConfig;
import com.amazonaws.services.elasticloadbalancingv2.model.InvalidConfigurationRequestException;
import com.amazonaws.services.elasticloadbalancingv2.model.Listener;
import com.amazonaws.services.elasticloadbalancingv2.model.LoadBalancer;
import com.amazonaws.services.elasticloadbalancingv2.model.Matcher;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyListenerRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyTargetGroupAttributesRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyTargetGroupAttributesResult;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyTargetGroupRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyTargetGroupResult;
import com.amazonaws.services.elasticloadbalancingv2.model.RegisterTargetsRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetDescription;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroup;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroupAttribute;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroupNotFoundException;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroupTuple;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetHealthDescription;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetTypeEnum;
import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DisableKeyRequest;
import com.amazonaws.services.kms.model.ScheduleKeyDeletionRequest;
import com.amazonaws.services.kms.model.Tag;
import com.amazonaws.services.route53.AmazonRoute53;
import com.amazonaws.services.route53.AmazonRoute53ClientBuilder;
import com.amazonaws.services.route53.model.GetHostedZoneRequest;
import com.amazonaws.services.route53.model.GetHostedZoneResult;
import com.amazonaws.services.securitytoken.AWSSecurityTokenService;
import com.amazonaws.services.securitytoken.AWSSecurityTokenServiceClientBuilder;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityRequest;
import com.amazonaws.services.securitytoken.model.GetCallerIdentityResult;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
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
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

// TODO - Better handling of UnauthorizedOperation. Ideally we should trigger alert so that
public class AWSCloudImpl implements CloudAPI {

  EncryptionAtRestManager keyManager;

  @Inject
  public AWSCloudImpl(EncryptionAtRestManager keyManager) {
    this.keyManager = keyManager;
  }

  public static final Logger LOG = LoggerFactory.getLogger(AWSCloudImpl.class);

  public AmazonElasticLoadBalancing getELBClient(Provider provider, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return AmazonElasticLoadBalancingClientBuilder.standard()
        .withRegion(regionCode)
        .withCredentials(credentialsProvider)
        .build();
  }

  // TODO use aws sdk 2.x and switch to async
  public AmazonEC2 getEC2Client(Provider provider, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return AmazonEC2ClientBuilder.standard()
        .withRegion(regionCode)
        .withCredentials(credentialsProvider)
        .build();
  }

  public AWSCloudTrail getCloudTrailClient(Provider provider, String region) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return AWSCloudTrailClientBuilder.standard()
        .withRegion(region)
        .withCredentials(credentialsProvider)
        .build();
  }

  public AmazonRoute53 getRoute53Client(Provider provider, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return AmazonRoute53ClientBuilder.standard()
        .withCredentials(credentialsProvider)
        .withRegion(regionCode)
        .build();
  }

  public AWSSecurityTokenService getStsClient(Provider provider, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(provider);
    return AWSSecurityTokenServiceClientBuilder.standard()
        .withCredentials(credentialsProvider)
        .withRegion(regionCode)
        .build();
  }

  private AWSCredentialsProvider getCredsOrFallbackToDefault(Provider provider) {
    String accessKeyId = provider.getDetails().getCloudInfo().getAws().awsAccessKeyID;
    String secretAccessKey = provider.getDetails().getCloudInfo().getAws().awsAccessKeySecret;
    if (checkKeysExists(provider)) {
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
        azByRegionMap.entrySet().parallelStream()
            .map(
                regionAZListEntry -> {
                  Filter locationFilter =
                      new Filter().withName("location").withValues(regionAZListEntry.getValue());
                  return getEC2Client(provider, regionAZListEntry.getKey().getCode())
                      .describeInstanceTypeOfferings(
                          new DescribeInstanceTypeOfferingsRequest()
                              .withLocationType(LocationType.AvailabilityZone)
                              .withFilters(locationFilter, instanceTypeFilter));
                })
            .collect(Collectors.toList());

    return results.stream()
        .flatMap(result -> result.getInstanceTypeOfferings().stream())
        .collect(
            groupingBy(
                InstanceTypeOffering::getInstanceType,
                mapping(InstanceTypeOffering::getLocation, toSet())));
  }

  @Override
  public boolean isValidCreds(Provider provider, String region) {
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
        // Disable and schedule the key for deletion. The minimum waiting period for
        // deletion is 7
        // days on AWS.
        String keyArn = result.getKeyMetadata().getArn();
        DisableKeyRequest req = new DisableKeyRequest().withKeyId(keyArn);
        kmsClient.disableKey(req);
        ScheduleKeyDeletionRequest scheduleKeyDeletionRequest =
            new ScheduleKeyDeletionRequest().withKeyId(keyArn).withPendingWindowInDays(7);
        kmsClient.scheduleKeyDeletion(scheduleKeyDeletionRequest);
        return true;
      }
    } catch (Exception e) {
      LOG.error(e.getMessage());
      return false;
    }
  }

  // Load balancer methods
  private LoadBalancer getLoadBalancerByName(AmazonElasticLoadBalancing lbClient, String lbName) {
    DescribeLoadBalancersRequest request = new DescribeLoadBalancersRequest().withNames(lbName);
    List<LoadBalancer> lbs = null;
    try {
      lbs = lbClient.describeLoadBalancers(request).getLoadBalancers();
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
    AmazonElasticLoadBalancing lbClient = getELBClient(provider, regionCode);
    return getLoadBalancerByName(lbClient, lbName);
  }

  private String getLoadBalancerArn(AmazonElasticLoadBalancing lbClient, String lbName) {
    try {
      return getLoadBalancerByName(lbClient, lbName).getLoadBalancerArn();
    } catch (Exception e) {
      String message = "Error executing task {getLoadBalancerByArn()}, error='{}'";
      throw new RuntimeException(message, e);
    }
  }

  // Listener methods
  private Listener createListener(
      AmazonElasticLoadBalancing lbClient,
      String lbName,
      String targetGroupArn,
      String protocol,
      int port) {
    String lbArn = getLoadBalancerArn(lbClient, lbName);
    TargetGroupTuple targetGroup = new TargetGroupTuple().withTargetGroupArn(targetGroupArn);
    ForwardActionConfig forwardConfig = new ForwardActionConfig().withTargetGroups(targetGroup);
    Action forwardToTargetGroup =
        new Action().withType(ActionTypeEnum.Forward).withForwardConfig(forwardConfig);
    CreateListenerRequest request =
        new CreateListenerRequest()
            .withLoadBalancerArn(lbArn)
            .withProtocol(protocol)
            .withPort(port)
            .withDefaultActions(forwardToTargetGroup);
    Listener listener = lbClient.createListener(request).getListeners().get(0);
    return listener;
  }

  private void setListenerTargetGroup(
      AmazonElasticLoadBalancing lbClient, String listenerArn, String targetGroupArn) {
    TargetGroupTuple targetGroup = new TargetGroupTuple().withTargetGroupArn(targetGroupArn);
    ForwardActionConfig forwardConfig = new ForwardActionConfig().withTargetGroups(targetGroup);
    Action forwardToTargetGroup =
        new Action().withType(ActionTypeEnum.Forward).withForwardConfig(forwardConfig);
    ModifyListenerRequest request =
        new ModifyListenerRequest()
            .withListenerArn(listenerArn)
            .withDefaultActions(forwardToTargetGroup);
    lbClient.modifyListener(request);
  }

  private List<Listener> getListeners(AmazonElasticLoadBalancing lbClient, String lbName) {
    String lbArn = getLoadBalancerArn(lbClient, lbName);
    DescribeListenersRequest request = new DescribeListenersRequest().withLoadBalancerArn(lbArn);
    List<Listener> listeners = lbClient.describeListeners(request).getListeners();
    return listeners;
  }

  @VisibleForTesting
  Listener getListenerByPort(AmazonElasticLoadBalancing lbClient, String lbName, int port) {
    List<Listener> listeners = getListeners(lbClient, lbName);
    for (Listener listener : listeners) {
      if (listener.getPort() == port) return listener;
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
      AmazonElasticLoadBalancing lbClient, String targetGroupArn) {
    // Get nodes in target group
    DescribeTargetHealthRequest request =
        new DescribeTargetHealthRequest().withTargetGroupArn(targetGroupArn);
    List<TargetHealthDescription> targetDescriptions =
        lbClient.describeTargetHealth(request).getTargetHealthDescriptions();
    List<TargetDescription> targets = new ArrayList<>();
    for (TargetHealthDescription targetDesc : targetDescriptions) {
      targets.add(targetDesc.getTarget());
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
      AmazonElasticLoadBalancing lbClient,
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
        if (target.getPort() != port) {
          removeInstanceIDs.add(target.getId());
        } else {
          currentInstanceIDs.add(target.getId());
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
      AmazonElasticLoadBalancing lbClient = getELBClient(provider, regionCode);
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
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
            setListenerTargetGroup(lbClient, listener.getListenerArn(), targetGroupArn);
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

  @Override
  public void validateInstanceTemplate(Provider provider, String instanceTemplate) {
    throw new PlatformServiceException(
        BAD_REQUEST, "Instance templates are currently not supported for AWS");
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
      AmazonElasticLoadBalancing lbClient,
      String targetGroupArn,
      String protocol,
      int port,
      List<String> instanceIDs,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    try {
      // Check target group settings
      TargetGroup targetGroup = getTargetGroup(lbClient, targetGroupArn);
      boolean validProtocol = targetGroup.getProtocol().equals(protocol);
      boolean validPort = targetGroup.getPort() == port;
      // If protocol or port incorrect then create new target group and update
      // listener
      if (!validProtocol || !validPort) {
        String targetGroupName = targetGroup.getTargetGroupName();
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
      AmazonElasticLoadBalancing lbClient,
      int port,
      TargetGroup targetGroup,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    boolean healthCheckModified = false;
    ModifyTargetGroupRequest modifyTargetGroupRequest =
        new ModifyTargetGroupRequest().withTargetGroupArn(targetGroup.getTargetGroupArn());
    Protocol healthCheckProtocol = healthCheckConfiguration.getHealthCheckProtocol();
    List<Integer> healthCheckPorts = healthCheckConfiguration.getHealthCheckPorts();
    // If there is no health probe corrosponding to the port that is being forwareded, we
    // select the 0th indexed port as the default health check for that forwarding rule
    // This is because this case would only arise in case of custom health checks
    // TODO: Find a way to link the correct custom health check to the correct forwarding rule
    Integer healthCheckPort = healthCheckPorts.isEmpty() ? port : healthCheckPorts.get(0);
    String healthCheckPath =
        healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(healthCheckPort);
    if (!targetGroup.getHealthCheckProtocol().equals(healthCheckProtocol.name())) {
      modifyTargetGroupRequest =
          modifyTargetGroupRequest.withHealthCheckProtocol(healthCheckProtocol.name());
      healthCheckModified = true;
    }
    if (!targetGroup.getHealthCheckPort().equals(Integer.toString(healthCheckPort))) {
      modifyTargetGroupRequest =
          modifyTargetGroupRequest.withHealthCheckPort(Integer.toString(healthCheckPort));
      healthCheckModified = true;
    }
    if (healthCheckProtocol == Protocol.HTTP
        && (targetGroup.getHealthCheckPath() == null
            || !targetGroup.getHealthCheckPath().equals(healthCheckPath))) {
      modifyTargetGroupRequest =
          modifyTargetGroupRequest
              .withHealthCheckPath(healthCheckPath)
              .withMatcher(new Matcher().withHttpCode("200"));
      healthCheckModified = true;
    }

    if (healthCheckModified) {
      try {
        ModifyTargetGroupResult result = lbClient.modifyTargetGroup(modifyTargetGroupRequest);
      } catch (TargetGroupNotFoundException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Target group not found: " + targetGroup.getTargetGroupArn());
      } catch (InvalidConfigurationRequestException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Invalid configuration request for target group: "
                + targetGroup.getTargetGroupArn()
                + " with attributes: "
                + modifyTargetGroupRequest.toString());
      } catch (Exception e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Error modifying target group: "
                + targetGroup.getTargetGroupArn()
                + " "
                + e.toString());
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
      AmazonElasticLoadBalancing lbClient,
      String lbName,
      String targetGroupName,
      String protocol,
      int port,
      List<String> instanceIDs,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    String vpc = getLoadBalancerByName(lbClient, lbName).getVpcId();
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
  void ensureTargetGroupAttributes(AmazonElasticLoadBalancing lbClient, String targetGroupArn) {
    ModifyTargetGroupAttributesRequest request =
        new ModifyTargetGroupAttributesRequest()
            .withTargetGroupArn(targetGroupArn)
            .withAttributes(
                Arrays.asList(
                    new TargetGroupAttribute()
                        .withKey("deregistration_delay.connection_termination.enabled")
                        .withValue("true")));
    try {
      ModifyTargetGroupAttributesResult result = lbClient.modifyTargetGroupAttributes(request);
    } catch (TargetGroupNotFoundException e) {
      LOG.warn("No such target group with targetGroupArn: " + request.getTargetGroupArn());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Target group not found: " + request.getTargetGroupArn());
    } catch (InvalidConfigurationRequestException e) {
      LOG.warn(
          "Attempt to set invalid configuration on target group with targetGroupArn: "
              + request.getTargetGroupArn());
      LOG.info("Target group attributes: " + request.getAttributes().toString());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to update attributes of target group.");
    }
  }

  // Target group methods
  private TargetGroup getTargetGroup(AmazonElasticLoadBalancing lbClient, String targetGroupArn) {
    DescribeTargetGroupsRequest request =
        new DescribeTargetGroupsRequest().withTargetGroupArns(targetGroupArn);
    return lbClient.describeTargetGroups(request).getTargetGroups().get(0);
  }

  @VisibleForTesting
  String getListenerTargetGroup(Listener listener) {
    List<Action> actions = listener.getDefaultActions();
    for (Action action : actions) {
      if (action.getType().equals(ActionTypeEnum.Forward.toString())) {

        return action.getTargetGroupArn();
      }
    }
    return null;
  }

  private String createTargetGroup(
      AmazonElasticLoadBalancing lbClient,
      String name,
      String protocol,
      int port,
      String vpc,
      NLBHealthCheckConfiguration healthCheckConfiguration) {
    CreateTargetGroupRequest targetGroupRequest =
        new CreateTargetGroupRequest()
            .withName(name)
            .withProtocol(protocol)
            .withPort(port)
            .withVpcId(vpc)
            .withTargetType(TargetTypeEnum.Instance)
            .withHealthCheckProtocol(healthCheckConfiguration.getHealthCheckProtocol().name())
            .withHealthCheckPort(Integer.toString(port));
    if (healthCheckConfiguration.getHealthCheckProtocol().equals(Protocol.HTTP)) {
      targetGroupRequest =
          targetGroupRequest
              .withHealthCheckPath(
                  healthCheckConfiguration.getHealthCheckPortsToPathsMap().get(port))
              .withMatcher(new Matcher().withHttpCode("200"));
    }
    TargetGroup targetGroup =
        lbClient.createTargetGroup(targetGroupRequest).getTargetGroups().get(0);
    String targetGroupArn = targetGroup.getTargetGroupArn();
    return targetGroupArn;
  }

  private void registerTargets(
      AmazonElasticLoadBalancing lbClient,
      String targetGroupArn,
      List<String> instanceIDs,
      int port) {
    if (CollectionUtils.isNotEmpty(instanceIDs)) {
      List<TargetDescription> targets = new ArrayList<>();
      for (String id : instanceIDs) {
        TargetDescription target = new TargetDescription().withId(id).withPort(port);
        targets.add(target);
      }
      RegisterTargetsRequest request =
          new RegisterTargetsRequest().withTargetGroupArn(targetGroupArn).withTargets(targets);
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
      AmazonElasticLoadBalancing lbClient, String targetGroupArn, List<String> instanceIDs) {
    List<TargetDescription> allTargets = getTargetGroupNodes(lbClient, targetGroupArn);
    List<TargetDescription> targets =
        allTargets.stream()
            .filter(t -> instanceIDs.contains(t.getId()))
            .collect(Collectors.toList());
    return targets;
  }

  private void deregisterTargets(
      AmazonElasticLoadBalancing lbClient, String targetGroupArn, List<String> instanceIDs) {
    if (CollectionUtils.isNotEmpty(instanceIDs)) {
      List<TargetDescription> targets = getTargets(lbClient, targetGroupArn, instanceIDs);
      DeregisterTargetsRequest request =
          new DeregisterTargetsRequest().withTargetGroupArn(targetGroupArn).withTargets(targets);
      lbClient.deregisterTargets(request);
    }
  }

  private void deregisterAllTargets(AmazonElasticLoadBalancing lbClient, String targetGroupArn) {
    List<TargetDescription> targets = getTargetGroupNodes(lbClient, targetGroupArn);
    DeregisterTargetsRequest request =
        new DeregisterTargetsRequest().withTargetGroupArn(targetGroupArn).withTargets(targets);
    lbClient.deregisterTargets(request);
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
  List<String> getInstanceIDs(AmazonEC2 ec2Client, List<NodeID> nodeIDs) {
    if (CollectionUtils.isEmpty(nodeIDs)) {
      return new ArrayList<>();
    }
    List<String> nodeNames =
        nodeIDs.stream().map(nodeId -> nodeId.getName()).collect(Collectors.toList());
    // Get instances by node name
    Filter filterName = new Filter("tag:Name").withValues(nodeNames);
    List<String> states = ImmutableList.of("pending", "running", "stopping", "stopped");
    Filter filterState = new Filter("instance-state-name").withValues(states);
    DescribeInstancesRequest instanceRequest =
        new DescribeInstancesRequest().withFilters(filterName, filterState);
    List<Reservation> reservations = ec2Client.describeInstances(instanceRequest).getReservations();
    // Filter by matching nodeUUIDs and older nodes missing UUID
    Map<NodeID, List<String>> nodeToInstances = new HashMap<>();
    for (Reservation r : reservations) {
      for (Instance i : r.getInstances()) {
        nodeToInstances
            .computeIfAbsent(getNodeIDs(i), k -> new ArrayList<>())
            .add(i.getInstanceId());
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
    for (com.amazonaws.services.ec2.model.Tag tag : instance.getTags()) {
      if (tag.getKey().equals("Name")) name = tag.getValue();
      if (tag.getKey().equals("node-uuid")) uuid = tag.getValue();
    }
    return new NodeID(name, uuid);
  }

  public GetCallerIdentityResult getStsClientOrBadRequest(Provider provider, Region region) {
    try {
      AWSSecurityTokenService stsClient = getStsClient(provider, region.getCode());
      return stsClient.getCallerIdentity(new GetCallerIdentityRequest());
    } catch (SdkClientException e) {
      LOG.error("AWS Provider validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "AWS access and secret keys validation failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeInstanceOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeInstancesRequest> dryRunResult =
          ec2Client.dryRun(new DescribeInstancesRequest());
      if (!dryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, dryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider validation dry run failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeInstances failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeImageOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeImagesRequest> dryRunResult =
          ec2Client.dryRun(new DescribeImagesRequest());
      if (!dryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, dryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider image dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeImages failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeInstanceTypesOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeInstanceTypesRequest> dryRunResult =
          ec2Client.dryRun(new DescribeInstanceTypesRequest());
      if (!dryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, dryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider instance types dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeInstanceTypes failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeVpcsOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeVpcsRequest> dryRunResult = ec2Client.dryRun(new DescribeVpcsRequest());
      if (!dryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, dryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider vpc dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeVpcs failed: " + e.getMessage());
    }
  }

  public boolean dryRunDescribeSubnetOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeSubnetsRequest> dryRunResult =
          ec2Client.dryRun(new DescribeSubnetsRequest());
      if (!dryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, dryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider Subnet dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS DescribeSubnets failed: " + e.getMessage());
    }
  }

  public boolean dryRunSecurityGroupOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeSecurityGroupsRequest> describeDryRunResult =
          ec2Client.dryRun(new DescribeSecurityGroupsRequest());
      DryRunResult<CreateSecurityGroupRequest> createDryRunResult =
          ec2Client.dryRun(new CreateSecurityGroupRequest());
      if (!describeDryRunResult.isSuccessful() && !createDryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, describeDryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider SecurityGroup dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS SecurityGroup failed: " + e.getMessage());
    }
  }

  public boolean dryRunKeyPairOrBadRequest(Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<DescribeKeyPairsRequest> describeDryRunResult =
          ec2Client.dryRun(new DescribeKeyPairsRequest());
      DryRunResult<CreateKeyPairRequest> createDryRunResult =
          ec2Client.dryRun(new CreateKeyPairRequest());
      if (!describeDryRunResult.isSuccessful() && !createDryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, describeDryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider KeyPair dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS KeyPair failed: " + e.getMessage());
    }
  }

  public boolean dryRunAuthorizeSecurityGroupIngressOrBadRequest(
      Provider provider, String regionCode) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      DryRunResult<AuthorizeSecurityGroupIngressRequest> describeDryRunResult =
          ec2Client.dryRun(new AuthorizeSecurityGroupIngressRequest());
      if (!describeDryRunResult.isSuccessful()) {
        throw new PlatformServiceException(
            BAD_REQUEST, describeDryRunResult.getDryRunResponse().getMessage());
      }
      return true;
    } catch (AmazonServiceException | PlatformServiceException e) {
      LOG.error("AWS Provider authorizeSecurityGroupIngress dry run validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Dry run of AWS AuthorizeSecurityGroupIngress failed: " + e.getMessage());
    }
  }

  public String getPrivateKeyAlgoOrBadRequest(String privateKeyString) {
    try {
      return CertificateHelper.getPrivateKey(privateKeyString).getAlgorithm();
    } catch (RuntimeException e) {
      LOG.error("Private key Algorithm extraction failed: ", e);
      throw new PlatformServiceException(BAD_REQUEST, "Could not fetch private key algorithm");
    }
  }

  public GetHostedZoneResult getHostedZoneOrBadRequest(
      Provider provider, Region region, String hostedZoneId) {
    try {
      AmazonRoute53 route53Client = getRoute53Client(provider, region.getCode());
      GetHostedZoneRequest request = new GetHostedZoneRequest().withId(hostedZoneId);
      return route53Client.getHostedZone(request);
    } catch (AmazonServiceException e) {
      LOG.error("Hosted Zone validation failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Hosted Zone validation failed: " + e.getMessage());
    }
  }

  public Image describeImageOrBadRequest(Provider provider, Region region, String imageId) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, region.getCode());
      DescribeImagesRequest request = new DescribeImagesRequest().withImageIds(imageId);
      DescribeImagesResult result = ec2Client.describeImages(request);
      return result.getImages().get(0);
    } catch (AmazonServiceException e) {
      LOG.error("AMI details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "AMI details extraction failed: " + e.getMessage());
    }
  }

  public List<SecurityGroup> describeSecurityGroupsOrBadRequest(Provider provider, Region region) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, region.getCode());
      DescribeSecurityGroupsRequest request =
          new DescribeSecurityGroupsRequest()
              .withGroupIds(Arrays.asList(region.getSecurityGroupId().split("\\s*,\\s*")));
      DescribeSecurityGroupsResult result = ec2Client.describeSecurityGroups(request);
      return result.getSecurityGroups();
    } catch (AmazonServiceException e) {
      LOG.error("Security group details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Security group extraction failed: " + e.getMessage());
    }
  }

  public Vpc describeVpcOrBadRequest(Provider provider, Region region) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, region.getCode());
      DescribeVpcsRequest request = new DescribeVpcsRequest().withVpcIds(region.getVnetName());
      DescribeVpcsResult result = ec2Client.describeVpcs(request);
      return result.getVpcs().get(0);
    } catch (AmazonServiceException e) {
      LOG.error("Vpc details extraction failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Vpc details extraction failed: " + e.getMessage());
    }
  }

  public List<Subnet> describeSubnetsOrBadRequest(Provider provider, Region region) {
    try {
      AmazonEC2 ec2Client = getEC2Client(provider, region.getCode());
      DescribeSubnetsRequest request =
          new DescribeSubnetsRequest()
              .withSubnetIds(
                  region.getZones().stream()
                      .map(zone -> zone.getSubnet())
                      .collect(Collectors.toList()));
      DescribeSubnetsResult result = ec2Client.describeSubnets(request);
      return result.getSubnets();
    } catch (AmazonServiceException e) {
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
        AmazonEC2 ec2Client = getEC2Client(provider, r.getCode());
        DeleteKeyPairRequest request = new DeleteKeyPairRequest().withKeyName(keyPairName);
        ec2Client.deleteKeyPair(request);
      }
    } catch (AmazonServiceException e) {
      LOG.error("Access Key deletion failed: ", e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Access Key deletion failed: " + e.getMessage());
    }
  }
}
