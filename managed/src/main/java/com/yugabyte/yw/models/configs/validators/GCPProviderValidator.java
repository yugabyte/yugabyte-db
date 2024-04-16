package com.yugabyte.yw.models.configs.validators;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.api.services.compute.model.Firewall;
import com.google.api.services.compute.model.FirewallPolicy;
import com.google.api.services.compute.model.FirewallPolicyRule;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPProjectApiClient;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudInfoInterface.VPCType;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class GCPProviderValidator extends ProviderFieldsValidator {

  private final GCPCloudImpl gcpCloudImpl;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public GCPProviderValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter, GCPCloudImpl gcpCloudImpl) {
    super(beanValidator, runtimeConfGetter);
    this.gcpCloudImpl = gcpCloudImpl;
    this.runtimeConfGetter = runtimeConfGetter;
  }

  @Override
  public void validate(Provider provider) {
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableGcpProviderValidation)) {
      log.warn("Validation is not enabled");
      return;
    }

    // add the jsonpath in the provider object
    JsonNode processedProvider = Util.addJsonPathToLeafNodes(Json.toJson(provider));
    JsonNode detailsJson = processedProvider.get("details");
    JsonNode cloudInfoJson = detailsJson.get("cloudInfo").get("gcp");

    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
    if (!gcpCloudImpl.isValidCreds(provider)) {
      // Further validation is not done since SA validation has failed
      String errorMsg = "Invalid GCP-SA Credentials [check logs for more info]";
      throwBeanProviderValidatorError(
          cloudInfoJson.get("useHostCredentials").get("jsonPath").asText(),
          errorMsg,
          Json.toJson(provider));
    }

    GCPProjectApiClient apiClient = new GCPProjectApiClient(runtimeConfGetter, provider);
    ArrayNode regionArrayJson = (ArrayNode) processedProvider.get("regions");
    ArrayNode imageBundleArrayJson = (ArrayNode) processedProvider.get("imageBundles");
    boolean enableVMOSPatching = runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching);

    // Verify role bindings [if the SA has req permissions to manage instances]
    boolean validateNewVpcPerms = getVpcType(provider) == VPCType.NEW;
    validateSAPermissions(apiClient, validateNewVpcPerms, validationErrorsMap, cloudInfoJson);

    validateVPC(provider, apiClient, validationErrorsMap, cloudInfoJson);

    // validate Region and its details
    validateRegions(provider, apiClient, validationErrorsMap, regionArrayJson);

    // Validate imageBundles
    validateImageBundles(
        provider, apiClient, validationErrorsMap, imageBundleArrayJson, enableVMOSPatching);

    // Validate if the keypair is an RSA keypair
    validatePrivateKeys(provider, processedProvider, validationErrorsMap);

    if (provider.getDetails() != null) {
      if (!enableVMOSPatching && provider.getDetails().getSshPort() != null) {
        // Verify if the ssh port is open
        String jsonPath = detailsJson.get("sshPort").get("jsonPath").asText();
        int sshPort = provider.getDetails().getSshPort();
        validateSshPort(provider, apiClient, validationErrorsMap, sshPort, jsonPath);
      }
      // Verify the existence of the tag to any of the firewall rules.
      validateFirewallTags(provider, apiClient, validationErrorsMap, cloudInfoJson);

      // Verify NTP server is valid IP/hostname
      validateNtpServers(provider, detailsJson, validationErrorsMap);
    }

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, Json.toJson(provider));
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // pass
  }

  private void validateSAPermissions(
      GCPProjectApiClient apiClient,
      Boolean validateNewVpcPerms,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode cloudInfoJson) {
    String jsonPath = cloudInfoJson.get("useHostCredentials").get("jsonPath").asText();
    try {
      List<String> permissionsSA =
          runtimeConfGetter
              .getStaticConf()
              .getStringList("yb.gcp.permissions_sa.min_permissions_sa");
      if (validateNewVpcPerms) {
        // Validate permissions to create new VPC, if new VPC option is selected
        List<String> permissionsNewVpc =
            runtimeConfGetter
                .getStaticConf()
                .getStringList("yb.gcp.permissions_sa.permissions_new_vpc");
        permissionsSA.addAll(permissionsNewVpc);
      }
      List<String> missingPermissions = apiClient.testIam(permissionsSA);
      if (missingPermissions.size() > 0) {
        String errorMsg =
            "The SA doesnt have the required permission(s): "
                + missingPermissions.stream().collect(Collectors.joining(", "));
        validationErrorsMap.put(jsonPath, errorMsg);
      }
    } catch (PlatformServiceException e) {
      validationErrorsMap.put(jsonPath, e.getMessage());
    }
  }

  private void validateVPC(
      Provider provider,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode cloudInfoJson) {
    // Validate existence of the VPC in the specified GCP project
    // [if existing VPC option is selected]
    String jsonPath = cloudInfoJson.get("destVpcId").get("jsonPath").asText();
    try {
      String vpcNetwork = getVpcNetwork(provider);
      if (getVpcType(provider) == VPCType.EXISTING
          && !apiClient.checkVpcExistence(getVpcProject(provider), vpcNetwork)) {
        String errorMsg =
            String.format(
                "The resource networks/%s is not found in the given GCP project", vpcNetwork);
        validationErrorsMap.put(jsonPath, errorMsg);
      }
    } catch (PlatformServiceException e) {
      validationErrorsMap.put(jsonPath, e.getMessage());
    }
  }

  private void validateRegions(
      Provider provider,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode regionArrayJson) {
    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      int regionInd = 0;
      for (Region region : provider.getRegions()) {
        JsonNode regionJson = regionArrayJson.get(regionInd++);
        // Validate existence of subnet in the specified project, region, VPC
        validateSubnetwork(provider, apiClient, region, validationErrorsMap, regionJson);

        // Validate existence of instanceTemplate in the specified project
        validateInstanceTempl(region, apiClient, validationErrorsMap, regionJson);
      }
    }
  }

  private void validateSubnetwork(
      Provider provider,
      GCPProjectApiClient apiClient,
      Region region,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode regionJson) {
    String vpcNetwork = getVpcNetwork(provider);
    String vpcProject = getVpcProject(provider);
    if (StringUtils.isNotEmpty(vpcNetwork)) {
      try {
        apiClient.validateSubnet(
            region.getCode(), region.getZones().get(0).getSubnet(), vpcNetwork, vpcProject);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(
            regionJson.get("zones").get(0).get("subnet").get("jsonPath").asText(), e.getMessage());
      }
    }
  }

  private void validateInstanceTempl(
      Region region,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode regionJson) {
    GCPRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(region);
    String instanceTemplate = regionCloudInfo.getInstanceTemplate();
    if (StringUtils.isNotEmpty(instanceTemplate)) {
      String jsonPath =
          regionJson
              .get("details")
              .get("cloudInfo")
              .get("gcp")
              .get("instanceTemplate")
              .get("jsonPath")
              .asText();
      try {
        if (!apiClient.checkInstanceTempelate(instanceTemplate)) {
          String errorMsg =
              String.format(
                  "The resource instanceTemplate/%s is not found in the given GCP project",
                  instanceTemplate);
          validationErrorsMap.put(jsonPath, errorMsg);
        }
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(jsonPath, e.getMessage());
      }
    }
  }

  private void validateImageBundles(
      Provider provider,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode imageBundleArrayJson,
      boolean enableVMOSPatching) {
    List<ImageBundle> imageBundles = provider.getImageBundles();
    int imageInd = 0;
    for (ImageBundle imageBundle : imageBundles) {
      JsonNode imageBundleJson = imageBundleArrayJson.get(imageInd++);
      String imageSelflink = imageBundle.getDetails().getGlobalYbImage();
      if (imageSelflink == null) {
        continue;
      }
      String jsonPath =
          imageBundleJson.get("details").get("globalYbImage").get("jsonPath").asText();
      try {
        apiClient.checkImageExistence(imageSelflink);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(jsonPath, e.getMessage());
      }
      try {
        if (enableVMOSPatching) {
          // Verify if the ssh port is open
          if (imageBundle.getDetails().getSshPort() == null) {
            continue;
          }
          int sshPort = imageBundle.getDetails().getSshPort();
          jsonPath = imageBundleJson.get("details").get("sshPort").get("jsonPath").asText();
          validateSshPort(provider, apiClient, validationErrorsMap, sshPort, jsonPath);
        }
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(jsonPath, e.getMessage());
      }
    }
  }

  /*
   * For Classic_VPC_Firewall rules:
   * first filter: rules associated with the network, direction=INGRESS, enabled
   * second filter: targetTags of the firewall rules, should be empty or ybFirewallTags::contains
   * Check for ip protocol: all/tcp
   * Check for port: [input can be a number, or range ]
   * If match found and action is allow, return true; action is deny, return false..
   * For Network_Firewall_Policy rules:
   * filter: direction=INGRESS, enabled, targetResources = null || contains networkurl,
   * and targetSecuretags = null
   * Check for ip protocol: all/tcp
   * Check for port: [input can be a number, or range ]
   * If match found and action is allow, return true; action is deny, return false..
   * The relative order for checking of Classic firewall rules and Network policy firewall rules
   * depends on the enforcement order of the network
   */
  private void validateSshPort(
      Provider provider,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      int sshPort,
      String jsonPath) {
    if (getVpcType(provider) != VPCType.NEW) {
      try {
        String vpcNetwork = getVpcNetwork(provider);
        String vpcProject = getVpcProject(provider);
        String networkSelfLink = String.format(GCPUtil.NETWORK_SELFLINK, vpcProject, vpcNetwork);
        // filter all the enabled INGRESS rules that are associated with the given VPC
        String filter =
            "(network eq " + networkSelfLink + ")(direction eq INGRESS)(disabled eq false)";
        List<Firewall> vpcFirewallRules = apiClient.getFirewallRules(filter, vpcProject);
        FirewallPolicy policy = apiClient.getNetworkFirewallPolicy(vpcNetwork, vpcProject);
        String networkFirewallPolicyEnforcementOrder =
            apiClient.getNetworkFirewallPolicyEnforcementOrder(vpcProject, vpcNetwork);
        String errorMsg;
        if (StringUtils.isEmpty(networkFirewallPolicyEnforcementOrder)) {
          errorMsg =
              String.format(
                  "The resource networks/%s is not found in the given GCP project", vpcNetwork);
          validationErrorsMap.put(jsonPath, errorMsg);
          return;
        } else if (networkFirewallPolicyEnforcementOrder.equals(
            GCPUtil.ENFORCEMENT_AFTER_CLASSIC_FIREWALL)) {
          // enforcement order: 1) VPC firewall rules 2) Global network firewall policy
          if (!checkSshPortFirewallRules(
              vpcFirewallRules,
              policy,
              provider,
              networkSelfLink,
              networkFirewallPolicyEnforcementOrder,
              sshPort)) {
            errorMsg =
                String.format(
                    "Connection(ssh) to host will fail at port %d as the port is not opened on any"
                        + " firewall",
                    sshPort);
            validationErrorsMap.put(jsonPath, errorMsg);
          }
        } else {
          // enforcement order: 1) Global network firewall policy 2) VPC firewall rules
          if (!checkSshPortFirewallPolicy(
              vpcFirewallRules,
              policy,
              provider,
              networkSelfLink,
              networkFirewallPolicyEnforcementOrder,
              sshPort)) {
            errorMsg =
                String.format(
                    "Connection(ssh) to host will fail at port %d as the port is not opened on any"
                        + " firewall",
                    sshPort);
            validationErrorsMap.put(jsonPath, errorMsg);
          }
        }
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(jsonPath, e.getMessage());
      }
    }
  }

  private void validateFirewallTags(
      Provider provider,
      GCPProjectApiClient apiClient,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode cloudInfoJson) {
    if (getVpcType(provider) != VPCType.NEW) {
      List<String> firewallTagsList = getFirewallTags(provider, false);
      if (!firewallTagsList.isEmpty()) {
        String jsonPath = cloudInfoJson.get("ybFirewallTags").get("jsonPath").asText();
        try {
          List<String> missingFirewallTags =
              apiClient.checkTagsExistence(
                  firewallTagsList, getVpcNetwork(provider), getVpcProject(provider));
          if (!missingFirewallTags.isEmpty()) {
            String missingTagsMessage =
                "Missing firewall tag(s) in the GCP project: "
                    + String.join(
                        ", ", missingFirewallTags); // Create a comma-separated list of missing tags
            validationErrorsMap.put(jsonPath, missingTagsMessage);
          }
        } catch (PlatformServiceException e) {
          validationErrorsMap.put(jsonPath, e.getMessage());
        }
      }
    }
  }

  private void validateNtpServers(
      Provider provider, JsonNode detailsJson, SetMultimap<String, String> validationErrorsMap) {
    if (provider.getDetails().ntpServers != null) {
      try {
        validateNTPServers(provider.getDetails().ntpServers);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put(
            detailsJson.get("ntpServers").get(0).get("jsonPath").asText(), e.getMessage());
      }
    }
  }

  private String getVpcNetwork(Provider provider) {
    String vpcNetwork = "";
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    if (gcpCloudInfo.getVpcType() == VPCType.EXISTING) {
      vpcNetwork = gcpCloudInfo.getDestVpcId();
    } else if (gcpCloudInfo.getVpcType() == VPCType.HOSTVPC) {
      vpcNetwork = gcpCloudInfo.getHostVpcId();
    }
    return vpcNetwork;
  }

  private String getVpcProject(Provider provider) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    String project = gcpCloudInfo.getGceProject();
    // Check for the existence of VPC in the Shared VPC Project if provided
    if (gcpCloudInfo.getSharedVPCProject() != null) {
      project = gcpCloudInfo.getSharedVPCProject();
    }
    return project;
  }

  private List<String> getFirewallTags(Provider provider, boolean addDefaultTags) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    List<String> firewallTagsList = new ArrayList<>();
    String firewallTags = gcpCloudInfo.getYbFirewallTags();
    if (StringUtils.isNotEmpty(firewallTags)) {
      firewallTagsList.addAll(Arrays.asList(firewallTags.split(",")));
    }
    // Add defaultTags if necessary
    if (addDefaultTags) {
      firewallTagsList.addAll(GCPUtil.YB_DEFAULT_INSTANCE_TAGS);
    }
    return firewallTagsList;
  }

  private VPCType getVpcType(Provider provider) {
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
    return gcpCloudInfo.getVpcType();
  }

  public boolean checkSshPortFirewallRules(
      List<Firewall> firewallRules,
      FirewallPolicy policy,
      Provider provider,
      String networkSelfLink,
      String networkFirewallPolicyEnforcementOrder,
      int sshPort) {
    if (!firewallRules.isEmpty()) {
      // If ybFirewallTags are not provided, "cluster-server" is the default tag that is added for
      // GCP instances.
      List<String> targetTags = getFirewallTags(provider, true);
      // A rule is applied either to all instances or if target tag(s) matches
      List<Firewall> filteredRules =
          firewallRules.stream()
              .filter(
                  rule ->
                      rule.getTargetTags() == null
                          || rule.getTargetTags().isEmpty()
                          || targetTags.stream().anyMatch(rule.getTargetTags()::contains))
              .collect(Collectors.toList());

      if (!filteredRules.isEmpty()) {
        for (Firewall firewallRule : filteredRules) {
          // Check if any rule explicitly denies sshPort/TCP
          if (firewallDeniedSshPort(firewallRule, sshPort)) {
            // sshPort/TCP is explicitly denied, return false
            return false;
          }
          // Check if any rule explicitly allows sshPort/TCP
          if (firewallAllowedSshPort(firewallRule, sshPort)) {
            // sshPort/TCP is explicitly allowed, return true
            return true;
          }
        }
      }
    }
    if (networkFirewallPolicyEnforcementOrder.equals(GCPUtil.ENFORCEMENT_AFTER_CLASSIC_FIREWALL)) {
      return checkSshPortFirewallPolicy(
          firewallRules,
          policy,
          provider,
          networkSelfLink,
          networkFirewallPolicyEnforcementOrder,
          sshPort);
    }
    return false;
  }

  // the code will return false if it finds a rule with: TCP or "all" protocol and
  // sshPort explicitly denied -or- sshPort implicitly denied due to no ports specified
  private boolean firewallDeniedSshPort(Firewall firewallRule, Integer sshPort) {
    return firewallRule.getDenied() != null
        && firewallRule.getDenied().stream()
            .anyMatch(
                denied ->
                    (denied.getIPProtocol().equalsIgnoreCase("all")
                            || denied.getIPProtocol().equalsIgnoreCase("tcp"))
                        && (denied.getPorts() == null
                            || denied.getPorts().isEmpty()
                            || isValidSshPort(denied.getPorts(), sshPort)));
  }

  // the code will return true if it finds a rule with: TCP or "all" protocol and
  // sshPort explicitly allowed -or- sshPort implicitly allowed due to no ports specified
  private boolean firewallAllowedSshPort(Firewall firewallRule, Integer sshPort) {
    return firewallRule.getAllowed() != null
        && firewallRule.getAllowed().stream()
            .anyMatch(
                allowed ->
                    (allowed.getIPProtocol().equalsIgnoreCase("all")
                            || allowed.getIPProtocol().equalsIgnoreCase("tcp"))
                        && (allowed.getPorts() == null
                            || allowed.getPorts().isEmpty()
                            || isValidSshPort(allowed.getPorts(), sshPort)));
  }

  // Example inputs include: ["22"], ["80","443"], and ["12345-12349"].
  private boolean isValidSshPort(List<String> inputPorts, Integer sshPort) {
    for (String port : inputPorts) {
      if (port.contains("-")) {
        String[] range = port.split("-");
        Integer lowerBound = Integer.parseInt(range[0]);
        Integer upperBound = Integer.parseInt(range[1]);
        if (sshPort >= lowerBound && sshPort <= upperBound) {
          return true;
        }
      } else if (Integer.parseInt(port) == sshPort) {
        return true;
      }
    }
    return false;
  }

  public boolean checkSshPortFirewallPolicy(
      List<Firewall> firewallRules,
      FirewallPolicy policy,
      Provider provider,
      String networkSelfLink,
      String networkFirewallPolicyEnforcementOrder,
      int sshPort) {
    if (policy != null) {
      // Filter the rules of the firewall policy to select only the ingress rules that are enabled;
      // targetResources == null || contains networkurl
      // and targetSecureTags should be null [applied to all the instances]
      List<FirewallPolicyRule> rules =
          policy.getRules().stream()
              .filter(
                  rule ->
                      rule.getDirection().equalsIgnoreCase("ingress")
                          && (rule.getDisabled() == null || !rule.getDisabled())
                          && (rule.getTargetResources() == null
                              || rule.getTargetResources().contains(networkSelfLink))
                          && rule.getTargetSecureTags() == null)
              .collect(Collectors.toList());
      if (!rules.isEmpty()) {
        for (FirewallPolicyRule rule : rules) {
          // Check IP protocol and ports
          boolean ipPortMatched =
              rule.getMatch().getLayer4Configs().stream()
                  .anyMatch(
                      config ->
                          (config.getIpProtocol().equalsIgnoreCase("all")
                                  || config.getIpProtocol().equalsIgnoreCase("tcp"))
                              && (config.getPorts() == null
                                  || config.getPorts().isEmpty()
                                  || isValidSshPort(config.getPorts(), sshPort)));
          if (ipPortMatched) {
            // If tcp/sshPort matched, return false if action is denied
            return !rule.getAction().equalsIgnoreCase("deny");
          }
        }
      }
    }
    if (networkFirewallPolicyEnforcementOrder.equals(GCPUtil.ENFORCEMENT_BEFORE_CLASSIC_FIREWALL)) {
      return checkSshPortFirewallRules(
          firewallRules,
          policy,
          provider,
          networkSelfLink,
          networkFirewallPolicyEnforcementOrder,
          sshPort);
    }
    return false;
  }
}
