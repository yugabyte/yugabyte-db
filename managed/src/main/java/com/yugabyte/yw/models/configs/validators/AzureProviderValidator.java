package com.yugabyte.yw.models.configs.validators;

import static com.azure.resourcemanager.marketplaceordering.models.OfferType.VIRTUALMACHINE;
import static com.yugabyte.yw.common.Util.addJsonPathToLeafNodes;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.azure.resourcemanager.AzureResourceManager;
import com.azure.resourcemanager.compute.models.GalleryImage;
import com.azure.resourcemanager.compute.models.GalleryImageVersion;
import com.azure.resourcemanager.compute.models.TargetRegion;
import com.azure.resourcemanager.compute.models.VirtualMachineImage;
import com.azure.resourcemanager.marketplaceordering.MarketplaceOrderingManager;
import com.azure.resourcemanager.marketplaceordering.models.AgreementTerms;
import com.azure.resourcemanager.network.models.Network;
import com.azure.resourcemanager.network.models.NetworkSecurityGroup;
import com.azure.resourcemanager.network.models.Subnet;
import com.azure.resourcemanager.privatedns.models.PrivateDnsZone;
import com.azure.resourcemanager.resources.models.ResourceGroup;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.azu.AZUResourceGroupApiClient;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class AzureProviderValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter confGetter;
  private final ConfigHelper configHelper;
  private final String AZURE_NETWORK_PREFIX =
      "^/subscriptions/([^/]+)/resourceGroups/([^/]+)/providers/Microsoft\\.Network/";
  private final String AZURE_COMPUTE_PREFIX =
      "^/subscriptions/([^/]+)/resourceGroups/([^/]+)/providers/Microsoft\\.Compute/";
  private final String AZURE_PRIVATE_DNS_ZONE_ID_REGEX =
      AZURE_NETWORK_PREFIX + "privateDnsZones/([^/]+)$";
  private final String AZURE_VNET_ID_REGEX = AZURE_NETWORK_PREFIX + "virtualNetworks/([^/]+)$";
  private final String AZURE_SUBNET_ID_REGEX =
      AZURE_NETWORK_PREFIX + "virtualNetworks/([^/]+)/subnets/([^/]+)$";
  private final String AZURE_SECURITY_GROUP_ID_REGEX =
      AZURE_NETWORK_PREFIX + "networkSecurityGroups/([^/]+)$";
  private final String AZURE_GALLERY_IMAGE_ID_REGEX =
      AZURE_COMPUTE_PREFIX + "galleries/([^/]+)/images/([^/]+)/versions/([^/]+)$";

  @Inject
  public AzureProviderValidator(
      RuntimeConfGetter confGetter, BeanValidator beanValidator, ConfigHelper configHelper) {
    super(beanValidator, confGetter);
    this.confGetter = confGetter;
    this.configHelper = configHelper;
  }

  @Override
  public void validate(Provider provider) {

    if (!confGetter.getGlobalConf(GlobalConfKeys.enableAzureProviderValidation)) {
      log.info("Azure provider validation not enabled!");
      return;
    }

    JsonNode providerJson = Json.toJson(provider);
    JsonNode processedJson = addJsonPathToLeafNodes(providerJson);

    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();
    AzureCloudInfo cloudInfo = CloudInfoInterface.get(provider);
    JsonNode cloudInfoJson = processedJson.get("details").get("cloudInfo").get("azu");

    String TENANT_ERROR = "Invalid tenant id provided.";
    String CLIENT_ERROR =
        String.format("Application with identifier '%s' was not found", cloudInfo.azuClientId);
    String SECRET_ERROR = "Invalid client secret provided.";
    String SUBSCRIPTION_ERROR = "subscriptionnotfound";

    // Try building the API Client to validate
    // Client ID, Client Secret, Subscription ID & Tenant ID
    try {
      // try to list RGs to verify creds
      AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudInfo);
      AzureResourceManager azure = client.getResourceManager(cloudInfo, 0);
      for (ResourceGroup rg : azure.resourceGroups().list()) {
        log.trace("RG: {} ", rg.name());
      }
    } catch (Exception e) {
      String error = e.getMessage().toLowerCase();
      log.error("Exception validating Azure provider", e);
      String clientJsonPath = cloudInfoJson.get("azuClientId").get("jsonPath").asText();
      // secret can be null in case of managed identity so create path from client jsonPath
      String secretJsonPath = clientJsonPath.replace("azuClientId", "azuClientSecret");
      String subIdJsonPath = cloudInfoJson.get("azuSubscriptionId").get("jsonPath").asText();

      if (error.contains("tenant id")) {
        String tenantJsonPath = cloudInfoJson.get("azuTenantId").get("jsonPath").asText();
        validationErrorsMap.put(
            tenantJsonPath,
            TENANT_ERROR
                + " Check to make sure you have the correct tenant ID."
                + " This may happen if there are no active subscriptions for the tenant.");
      } else if (error.contains(CLIENT_ERROR.toLowerCase())) {
        validationErrorsMap.put(
            clientJsonPath,
            CLIENT_ERROR
                + ".This can happen if the application has not been installed by the administrator"
                + " of the tenant or consented to by any user in the tenant.");
      } else if (error.contains(SECRET_ERROR.toLowerCase())) {
        validationErrorsMap.put(secretJsonPath, SECRET_ERROR);
      } else if (error.contains(SUBSCRIPTION_ERROR)) {
        validationErrorsMap.put(subIdJsonPath, "Subscription ID not found!");
      } else {
        // to make sure error doesn't go unescaped.
        throw e;
      }
    }

    // throw error early since it affects rest of the validation
    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
    }

    AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudInfo);
    AzureResourceManager azure = client.getResourceManager(cloudInfo, 3);

    String resourceGroup = cloudInfo.getAzuRG();
    String subscriptionID = cloudInfo.getAzuSubscriptionId();

    // verify subscription id exists
    String subIdJsonPath = cloudInfoJson.get("azuSubscriptionId").get("jsonPath").asText();
    validateSubscriptionID(azure, subscriptionID, subIdJsonPath, providerJson, validationErrorsMap);

    String rgJsonPath = cloudInfoJson.get("azuRG").get("jsonPath").asText();
    validateResourceGroup(
        azure,
        resourceGroup,
        subscriptionID,
        cloudInfo,
        rgJsonPath,
        validationErrorsMap,
        providerJson);

    validateNtpServers(provider, processedJson, validationErrorsMap);

    validateDnszone(cloudInfo, azure, cloudInfoJson, validationErrorsMap);

    validatePrivateKeys(provider, processedJson, validationErrorsMap);

    validateImageBundles(
        provider,
        azure,
        client.getAzureMarketplaceOrderingManager(),
        cloudInfo,
        processedJson,
        validationErrorsMap);

    validateNetworkResource(cloudInfo, cloudInfoJson, providerJson, azure, validationErrorsMap);
    if (cloudInfo.getAzuNetworkSubscriptionId() != null) {
      subscriptionID = cloudInfo.getAzuNetworkSubscriptionId();
    }
    if (cloudInfo.getAzuNetworkRG() != null) {
      resourceGroup = cloudInfo.getAzuNetworkRG();
    }

    ArrayNode regionArrayJson = (ArrayNode) processedJson.get("regions");
    validateRegionDetails(
        azure,
        provider,
        resourceGroup,
        subscriptionID,
        cloudInfo,
        regionArrayJson,
        validationErrorsMap);

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // pass
  }

  /**
   * Verify that all the Images are available in all regions. Check if vm image has terms. If yes,
   * check if those are accepted.
   *
   * @param provider
   * @param azure
   * @param manager
   * @param validationErrorsMap
   */
  private void validateImageBundles(
      Provider provider,
      AzureResourceManager azure,
      MarketplaceOrderingManager manager,
      AzureCloudInfo cloudInfo,
      JsonNode processedJson,
      SetMultimap<String, String> validationErrorsMap) {

    int index = -1;
    ArrayNode imageBundleJson = (ArrayNode) processedJson.get("imageBundles");
    for (ImageBundle imageBundle : provider.getImageBundles()) {
      String image = imageBundle.getDetails().getGlobalYbImage();
      index++;
      if (image == null) {
        continue;
      }
      String imageJsonPath =
          imageBundleJson.get(index).get("details").get("globalYbImage").get("jsonPath").asText();
      Matcher matcher = Pattern.compile(AZURE_GALLERY_IMAGE_ID_REGEX).matcher(image);
      if (matcher.matches()) {
        String subscriptionId = matcher.group(1);
        String resourceGroupName = matcher.group(2);
        String galleryName = matcher.group(3);
        String imageName = matcher.group(4);
        String version = matcher.group(5);
        // Generate AzureResourceManager from gallery image details
        AzureCloudInfo galleryCloudInfo =
            cloudInfo.toBuilder()
                .azuSubscriptionId(subscriptionId)
                .azuRG(resourceGroupName)
                .build();
        AZUResourceGroupApiClient galleryClient = new AZUResourceGroupApiClient(galleryCloudInfo);
        AzureResourceManager galleryAzure = galleryClient.getResourceManager(galleryCloudInfo, 3);
        MarketplaceOrderingManager galleryManager =
            galleryClient.getAzureMarketplaceOrderingManager();
        GalleryImage galleryImage =
            galleryAzure.galleryImages().getByGallery(resourceGroupName, galleryName, imageName);
        GalleryImageVersion galleryImageVersion = galleryImage.getVersion(version);
        List<TargetRegion> availableRegions = galleryImageVersion.availableRegions();

        // Check if all provider regions are available in gallery image
        for (Region region : provider.getRegions()) {
          Map<String, Object> regionMetadata = configHelper.getRegionMetadata(CloudType.azu);
          JsonNode metaData = Json.toJson(regionMetadata.get(region.getCode()));
          String regionName = metaData.get("name").asText();
          boolean regionAvailable =
              availableRegions.stream()
                  .map(TargetRegion::name)
                  .anyMatch(name -> name.equalsIgnoreCase(regionName));

          if (!regionAvailable) {
            String err =
                String.format("Gallery image %s is not available in region %s", image, regionName);
            validationErrorsMap.put(imageJsonPath, err);
          }
        }

        // make sure ImagePurchasePlan is agreed
        if (galleryImage.purchasePlan() != null) {
          String publisher = galleryImage.purchasePlan().publisher();
          String offer = galleryImage.purchasePlan().product();
          String plan = galleryImage.purchasePlan().name();
          try {
            AgreementTerms terms =
                galleryManager.marketplaceAgreements().get(VIRTUALMACHINE, publisher, offer, plan);
            if (!terms.accepted()) {
              throw new Exception("Marketplace agreement terms not accepted.");
            }
          } catch (Exception e) {
            String err = String.format("Need to accept the terms for the image %s", image);
            validationErrorsMap.put(imageJsonPath, err);
          }
        }
        return;
      }
      String parts[] = image.strip().split(":");
      try {
        // verify correct format
        // sample image -> tunnelbiz:fedora:fedoragen2:latest
        if (parts.length != 4) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Wrong Azure image specified."
                  + "Azure images are in the format \"Publisher:Offer:Sku:Version\".");
        }
        String publisher = parts[0], offer = parts[1], sku = parts[2], version = parts[3];
        // verify the image is available in all regions
        for (Region region : provider.getRegions()) {
          try {
            VirtualMachineImage vmImage =
                azure
                    .virtualMachineImages()
                    .getImage(region.getCode(), publisher, offer, sku, version);
            // check if we need to accept the terms
            if (vmImage.plan() != null) {
              try {
                String planID = vmImage.plan().name();
                AgreementTerms terms =
                    manager.marketplaceAgreements().get(VIRTUALMACHINE, publisher, offer, planID);
                if (!terms.accepted()) {
                  throw new Exception("Marketplace agreement terms not accepted.");
                }
              } catch (Exception e) {
                String err = String.format("Need to accept the terms for the image %s", image);
                validationErrorsMap.put(imageJsonPath, err);
              }
            }
          } catch (Exception e) {
            String err =
                String.format("VM Image %s not found in region %s", image, region.getCode());
            throw new PlatformServiceException(BAD_REQUEST, err);
          }
        }
      } catch (Exception e) {
        validationErrorsMap.put(imageJsonPath, e.getMessage());
      }
    }
  }

  /**
   * verify that the given resoruce group is present in the given subscription
   *
   * @param resourceGroup
   * @param subscriptionID
   * @param info
   * @param isNetworkRg
   * @param validationErrorsMap
   */
  private void validateResourceGroup(
      AzureResourceManager azure,
      String resourceGroup,
      String subscriptionID,
      AzureCloudInfo info,
      String errKey,
      SetMultimap<String, String> validationErrorsMap,
      JsonNode providerJson) {
    try {
      if (subscriptionID.equals(info.azuNetworkSubscriptionId)) {
        AzureCloudInfo cloudinfo =
            info.toBuilder().azuRG(resourceGroup).azuSubscriptionId(subscriptionID).build();
        AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudinfo);
        azure = client.getAzureResourceManager();
      }
      // validate resource group exists
      if (!azure.resourceGroups().contain(resourceGroup)) {
        String err =
            String.format(
                "Resource group %s not found in Subscription %s", resourceGroup, subscriptionID);
        validationErrorsMap.put(errKey, err);
        throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
      }
    } catch (Exception e) {
      throw e;
    }
  }

  /**
   * Validate presence of vnets, subnets, security group. If Network {resource group, subscription
   * id} are provided we look for vnets, subnets in these.
   *
   * @param provider
   * @param resourceGroup
   * @param subscriptionID
   * @param info
   * @param validationErrorsMap
   */
  private void validateRegionDetails(
      AzureResourceManager azure,
      Provider provider,
      String resourceGroup,
      String subscriptionID,
      AzureCloudInfo info,
      ArrayNode regionArrayJson,
      SetMultimap<String, String> validationErrorsMap) {
    String baseResourceGroup = resourceGroup;
    try {
      if (subscriptionID.equals(info.azuNetworkSubscriptionId)) {
        AzureCloudInfo cloudinfo =
            info.toBuilder().azuRG(resourceGroup).azuSubscriptionId(subscriptionID).build();
        AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudinfo);
        azure = client.getAzureResourceManager();
      }
      int regionIndex = 0;

      for (Region region : provider.getRegions()) {
        resourceGroup = baseResourceGroup;
        AzureRegionCloudInfo regionInfo = region.getDetails().cloudInfo.azu;
        JsonNode regionJson = regionArrayJson.get(regionIndex++);
        JsonNode cloudInfoJson = regionJson.get("details").get("cloudInfo").get("azu");
        String securityGroup = regionInfo.getSecurityGroupId();

        if (regionInfo.getAzuRGOverride() != null) {
          resourceGroup = regionInfo.getAzuRGOverride();
          AzureCloudInfo.AzureCloudInfoBuilder cloudInfoBuilder =
              info.toBuilder().azuRG(resourceGroup);
          AZUResourceGroupApiClient client =
              new AZUResourceGroupApiClient(cloudInfoBuilder.build());
          azure = client.getAzureResourceManager();

          if (!azure.resourceGroups().contain(resourceGroup)) {
            String resourceGroupOverrideJsonPath =
                cloudInfoJson.get("azuRGOverride").get("jsonPath").asText();
            String err =
                String.format(
                    "Resource group %s not found in Subscription %s",
                    resourceGroup, subscriptionID);
            validationErrorsMap.put(resourceGroupOverrideJsonPath, err);
          }
        }

        if (regionInfo.getAzuNetworkRGOverride() != null) {
          resourceGroup = regionInfo.getAzuNetworkRGOverride();
          AzureCloudInfo.AzureCloudInfoBuilder cloudInfoBuilder =
              info.toBuilder().azuRG(resourceGroup);
          AZUResourceGroupApiClient client =
              new AZUResourceGroupApiClient(cloudInfoBuilder.build());
          azure = client.getAzureResourceManager();

          if (!azure.resourceGroups().contain(resourceGroup)) {
            String networkGroupOverrideJsonPath =
                cloudInfoJson.get("azuNetworkRGOverride").get("jsonPath").asText();
            String err =
                String.format(
                    "Resource group %s not found in Subscription %s",
                    resourceGroup, subscriptionID);
            validationErrorsMap.put(networkGroupOverrideJsonPath, err);
          }
        }

        // verify security group exists
        if (securityGroup != null) {
          String sgResourceGroup = resourceGroup;
          String sgName = securityGroup;
          try {
            Matcher sgMatcher =
                Pattern.compile(AZURE_SECURITY_GROUP_ID_REGEX).matcher(securityGroup);
            if (sgMatcher.matches()) {
              sgResourceGroup = sgMatcher.group(2);
              sgName = sgMatcher.group(3);
              NetworkSecurityGroup azureNetworkSecurityGroup =
                  azure.networkSecurityGroups().getById(securityGroup);
            } else {
              NetworkSecurityGroup azureNetworkSecurityGroup =
                  azure.networkSecurityGroups().getByResourceGroup(sgResourceGroup, securityGroup);
            }
          } catch (Exception e) {
            String sgJsonPath = cloudInfoJson.get("securityGroupId").get("jsonPath").asText();
            String err =
                String.format(
                    "Security Group: %s not found in Resource Group: %s!", sgName, sgResourceGroup);
            validationErrorsMap.put(sgJsonPath, err);
          }
        }

        // verify virtual network exists
        String vnet = regionInfo.getVnet();
        String vnetJsonPath = cloudInfoJson.get("vnet").get("jsonPath").asText();
        String vnetResourceGroup = resourceGroup;
        String vnetName = vnet;
        try {
          Network network;
          Matcher vnetMatcher = Pattern.compile(AZURE_VNET_ID_REGEX).matcher(vnet);
          if (vnetMatcher.matches()) {
            vnetResourceGroup = vnetMatcher.group(2);
            vnetName = vnetMatcher.group(3);
            network = azure.networks().getById(vnet);
          } else {
            network = azure.networks().getByResourceGroup(vnetResourceGroup, vnetName);
          }
          // verify all subnets exist
          int zoneIndex = 0;
          ArrayNode zoneArrayJson = (ArrayNode) regionJson.get("zones");
          Map<String, Subnet> subnets = network.subnets();
          for (AvailabilityZone zone : region.getZones()) {
            String subnet = zone.getSubnet();
            String subnetJsonPath =
                zoneArrayJson.get(zoneIndex++).get("subnet").get("jsonPath").asText();
            Matcher subnetMatcher = Pattern.compile(AZURE_SUBNET_ID_REGEX).matcher(subnet);
            if (subnetMatcher.matches()) {
              vnetName = subnetMatcher.group(3);
              String subnetName = subnetMatcher.group(4);
              try {
                azure.genericResources().getById(subnet);
              } catch (Exception e) {
                log.error("Exception validating subnet: {}", e.getMessage());
                String err =
                    String.format(
                        "Subnet %s not found under Virtual Network %s!", subnetName, vnetName);
                validationErrorsMap.put(subnetJsonPath, err);
              }
            } else {
              if (!subnets.containsKey(subnet)) {
                String err =
                    String.format(
                        "Subnet %s not found under Virtual Network %s!", subnet, vnetName);
                validationErrorsMap.put(subnetJsonPath, err);
              }
            }
          }
        } catch (Exception e) {
          log.error("Exception validating virtual network: {}", e.getMessage());
          String err =
              String.format(
                  "Virtual Network: %s not found in Resource Group: %s",
                  vnetName, vnetResourceGroup);
          validationErrorsMap.put(vnetJsonPath, err);
        }
      }
    } catch (Exception e) {
      throw e;
    }
  }

  private void validateNtpServers(
      Provider provider, JsonNode processedJson, SetMultimap<String, String> validationErrorsMap) {
    if (provider.getDetails() != null && provider.getDetails().ntpServers != null) {
      try {
        validateNTPServers(provider.getDetails().ntpServers);
      } catch (PlatformServiceException e) {
        ArrayNode ntpArrayJson = (ArrayNode) processedJson.get("details").get("ntpServers");
        // since there is single form field for ntp servers  on UI, we can use the jsonPath of the
        // first element
        String ntpJsonPath = ntpArrayJson.get(0).get("jsonPath").asText();
        validationErrorsMap.put(ntpJsonPath, e.getMessage());
      }
    }
  }

  private void validateSubscriptionID(
      AzureResourceManager azure,
      String subID,
      String errKey,
      JsonNode providerJson,
      SetMultimap<String, String> validationErrorsMap) {
    try {
      azure.genericResources().manager().subscriptionClient().getSubscriptions().get(subID);
    } catch (Exception e) {
      validationErrorsMap.put(errKey, "Subscription ID not found!");
      throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
    }
  }

  private void validateDnszone(
      AzureCloudInfo cloudInfo,
      AzureResourceManager azure,
      JsonNode cloudInfoJson,
      SetMultimap<String, String> validationErrorsMap) {
    String privateDnsZone = cloudInfo.getAzuHostedZoneId();
    String resourceGroup = cloudInfo.getAzuRG();
    if (privateDnsZone != null) {
      try {
        if (Pattern.compile(AZURE_PRIVATE_DNS_ZONE_ID_REGEX).matcher(privateDnsZone).matches()) {
          PrivateDnsZone zone = azure.privateDnsZones().getById(privateDnsZone);
        } else {
          PrivateDnsZone zone =
              azure.privateDnsZones().getByResourceGroup(resourceGroup, privateDnsZone);
        }
      } catch (Exception e) {
        String dnsZoneJsonPath = cloudInfoJson.get("azuHostedZoneId").get("jsonPath").asText();
        String err =
            String.format(
                "Private DNS Zone: %s not found in Resource Group: %s",
                privateDnsZone, resourceGroup);
        validationErrorsMap.put(dnsZoneJsonPath, err);
      }
    }
  }

  private void validateNetworkResource(
      AzureCloudInfo cloudInfo,
      JsonNode cloudInfoJson,
      JsonNode providerJson,
      AzureResourceManager azure,
      SetMultimap<String, String> validationErrorsMap) {
    String subscriptionID = cloudInfo.getAzuSubscriptionId();
    if (cloudInfo.getAzuNetworkSubscriptionId() != null) {
      // verify network subscription id exists
      subscriptionID = cloudInfo.getAzuNetworkSubscriptionId();
      String networkSubIdJsonPath =
          cloudInfoJson.get("azuNetworkSubscriptionId").get("jsonPath").asText();
      validateSubscriptionID(
          azure, subscriptionID, networkSubIdJsonPath, providerJson, validationErrorsMap);

      // if network subscription id is provided than resource group is required as well
      if (cloudInfo.getAzuNetworkRG() == null) {
        // since network rg is null, its jsonpath will not be present in the processed json
        // so we use the network sub id jsonpath to generate it
        String networkRgJsonPath =
            networkSubIdJsonPath.replace("azuNetworkSubscriptionId", "azuNetworkRG");
        validationErrorsMap.put(
            networkRgJsonPath,
            "Network Resource Group cannot be empty if Network Subscription ID is provided!");
      }
    }

    if (cloudInfo.getAzuNetworkRG() != null) {
      String resourceGroup = cloudInfo.getAzuNetworkRG();
      // verify network resource group exists
      String networkRgJsonPath = cloudInfoJson.get("azuNetworkRG").get("jsonPath").asText();
      validateResourceGroup(
          azure,
          resourceGroup,
          subscriptionID,
          cloudInfo,
          networkRgJsonPath,
          validationErrorsMap,
          providerJson);
    }
  }
}
