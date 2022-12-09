package com.yugabyte.yw.cloud.aws;

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
import com.amazonaws.services.ec2.model.Instance;
import com.amazonaws.services.ec2.model.InstanceTypeOffering;
import com.amazonaws.services.ec2.model.LocationType;
import com.amazonaws.services.ec2.model.Reservation;
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
import com.amazonaws.services.elasticloadbalancingv2.model.Listener;
import com.amazonaws.services.elasticloadbalancingv2.model.LoadBalancer;
import com.amazonaws.services.elasticloadbalancingv2.model.ModifyListenerRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.RegisterTargetsRequest;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetDescription;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroup;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetGroupTuple;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetHealthDescription;
import com.amazonaws.services.elasticloadbalancingv2.model.TargetTypeEnum;
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
import com.yugabyte.yw.models.helpers.NodeID;
import org.apache.commons.collections.CollectionUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import static java.util.stream.Collectors.groupingBy;
import static java.util.stream.Collectors.mapping;
import static java.util.stream.Collectors.toSet;

// TODO - Better handling of UnauthorizedOperation. Ideally we should trigger alert so that
// site admin knows about it
public class AWSCloudImpl implements CloudAPI {
  public static final Logger LOG = LoggerFactory.getLogger(AWSCloudImpl.class);

  public AmazonElasticLoadBalancing getELBClient(Provider provider, String regionCode) {
    return getELBClientInternal(provider.getUnmaskedConfig(), regionCode);
  }

  private AmazonElasticLoadBalancing getELBClientInternal(
      Map<String, String> config, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(config);
    return AmazonElasticLoadBalancingClientBuilder.standard()
        .withRegion(regionCode)
        .withCredentials(credentialsProvider)
        .build();
  }

  // TODO use aws sdk 2.x and switch to async
  public AmazonEC2 getEC2Client(Provider provider, String regionCode) {
    return getEC2ClientInternal(provider.getUnmaskedConfig(), regionCode);
  }

  private AmazonEC2 getEC2ClientInternal(Map<String, String> config, String regionCode) {
    AWSCredentialsProvider credentialsProvider = getCredsOrFallbackToDefault(config);
    return AmazonEC2ClientBuilder.standard()
        .withRegion(regionCode)
        .withCredentials(credentialsProvider)
        .build();
  }

  // TODO: move to some common utils
  private static AWSCredentialsProvider getCredsOrFallbackToDefault(Map<String, String> config) {
    String accessKeyId = config.get("AWS_ACCESS_KEY_ID");
    String secretAccessKey = config.get("AWS_SECRET_ACCESS_KEY");
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
                  return getEC2Client(provider, regionAZListEntry.getKey().code)
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

  // Load balancer methods
  private LoadBalancer getLoadBalancerByName(AmazonElasticLoadBalancing lbClient, String lbName) {
    DescribeLoadBalancersRequest request = new DescribeLoadBalancersRequest().withNames(lbName);
    List<LoadBalancer> lbs = null;
    try {
      lbs = lbClient.describeLoadBalancers(request).getLoadBalancers();
      System.out.print(lbs);
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

  private Listener getListenerByPort(AmazonElasticLoadBalancing lbClient, String lbName, int port) {
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
          instanceIDs
              .stream()
              .filter(i -> !currentInstanceIDs.contains(i))
              .collect(Collectors.toList());
      removeInstanceIDs.addAll(
          currentInstanceIDs
              .stream()
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
   * @param nodeNames the DB node names.
   * @param nodeIDs the DB node IDs (name, uuid).
   * @param protocol the listening protocol.
   * @param ports the listening ports enabled (YSQL, YCQL, YEDIS).
   */
  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      List<String> nodeNames,
      List<NodeID> nodeIDs,
      String protocol,
      List<Integer> ports) {
    try {
      // Get aws clients
      AmazonElasticLoadBalancing lbClient = getELBClient(provider, regionCode);
      AmazonEC2 ec2Client = getEC2Client(provider, regionCode);
      // Get EC2 node instances
      List<String> instanceIDs = getInstanceIDs(ec2Client, nodeNames, nodeIDs);
      // Check for listeners on each enabled port
      for (int port : ports) {
        Listener listener = getListenerByPort(lbClient, lbName, port);
        // If no listener exists for a port, create target group and listener
        // else check target group settings and add/remove nodes from target group
        String targetGroupName = "tg-" + UUID.randomUUID();
        if (listener == null) {
          String targetGroupArn =
              createNodeGroup(lbClient, lbName, targetGroupName, protocol, port, instanceIDs);
          createListener(lbClient, lbName, targetGroupArn, protocol, port);
        } else {
          // Check if listener has target group otherwise create one
          String targetGroupArn = getListenerTargetGroup(listener);
          if (targetGroupArn == null) {
            targetGroupArn =
                createNodeGroup(lbClient, lbName, targetGroupName, protocol, port, instanceIDs);
            setListenerTargetGroup(lbClient, listener.getListenerArn(), targetGroupArn);
          } else {
            // Check node group
            checkNodeGroup(lbClient, targetGroupArn, protocol, port, instanceIDs);
          }
        }
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
  private void checkNodeGroup(
      AmazonElasticLoadBalancing lbClient,
      String targetGroupArn,
      String protocol,
      int port,
      List<String> instanceIDs) {
    try {
      // Check target group settings
      TargetGroup targetGroup = getTargetGroup(lbClient, targetGroupArn);
      boolean validProtocol = targetGroup.getProtocol().equals(protocol);
      boolean validPort = targetGroup.getPort() == port;
      // If protocol or port incorrect then create new target group and update listener
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
      }
    } catch (Exception e) {
      String message = "Error executing task {checkNodeGroup()}, error='{}'";
      throw new RuntimeException(message, e);
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
      List<String> instanceIDs) {
    String vpc = getLoadBalancerByName(lbClient, lbName).getVpcId();
    String targetGroupArn = createTargetGroup(lbClient, targetGroupName, protocol, port, vpc);
    registerTargets(lbClient, targetGroupArn, instanceIDs, port);
    return targetGroupArn;
  }

  // Target group methods
  private TargetGroup getTargetGroup(AmazonElasticLoadBalancing lbClient, String targetGroupArn) {
    DescribeTargetGroupsRequest request =
        new DescribeTargetGroupsRequest().withTargetGroupArns(targetGroupArn);
    return lbClient.describeTargetGroups(request).getTargetGroups().get(0);
  }

  private String getListenerTargetGroup(Listener listener) {
    List<Action> actions = listener.getDefaultActions();
    for (Action action : actions) {
      if (action.getType().equals(ActionTypeEnum.Forward.toString())) {

        return action.getTargetGroupArn();
      }
    }
    return null;
  }

  private String createTargetGroup(
      AmazonElasticLoadBalancing lbClient, String name, String protocol, int port, String vpc) {
    CreateTargetGroupRequest targetGroupRequest =
        new CreateTargetGroupRequest()
            .withName(name)
            .withProtocol(protocol)
            .withPort(port)
            .withVpcId(vpc)
            .withTargetType(TargetTypeEnum.Instance);
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
        allTargets
            .stream()
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
   * @param nodeNames the list of node names.
   * @param nodeIDs the node IDs (name, uuid).
   * @return a list. The node instance IDs.
   */
  private List<String> getInstanceIDs(
      AmazonEC2 ec2Client, List<String> nodeNames, List<NodeID> nodeIDs) throws Exception {
    // Get instances by node name
    Filter filterName = new Filter("tag:Name").withValues(nodeNames);
    DescribeInstancesRequest instanceRequest =
        new DescribeInstancesRequest().withFilters(filterName);
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
        throw new Exception("Failure: node instance with name \"" + id.getName() + "\" not found");
      } else if (ids.size() > 1) {
        throw new Exception(
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
}
