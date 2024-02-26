package com.yugabyte.yw.models.configs.validators;

import static com.yugabyte.yw.common.Util.addJsonPathToLeafNodes;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.azure.core.management.exception.ManagementException;
import com.azure.resourcemanager.AzureResourceManager;
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
import com.yugabyte.yw.common.BeanValidator;
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
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class AzureProviderValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter confGetter;

  @Inject
  public AzureProviderValidator(RuntimeConfGetter confGetter, BeanValidator beanValidator) {
    super(beanValidator, confGetter);
    this.confGetter = confGetter;
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

    String TENANT_ERROR = String.format("Tenant '%s' not found.", cloudInfo.azuTenantId);
    String CLIENT_ERROR =
        String.format("Application with identifier '%s' was not found", cloudInfo.azuClientId);
    String SECRET_ERROR = "Invalid client secret provided.";

    // Try building the API Client to validate
    // Client ID, Client Secret, Subscription ID & Tenant ID
    AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudInfo);
    AzureResourceManager azure = client.getResourceManager(cloudInfo, 0);
    try {
      // try to list RGs to verify creds
      for (ResourceGroup rg : azure.resourceGroups().list()) {
        String rgName = rg.name();
      }
    } catch (Exception e) {
      String error = e.getMessage();
      String clientJsonPath = cloudInfoJson.get("azuClientId").get("jsonPath").asText();
      // secret can be null in case of managed identity so create path from client jsonPath
      String secretJsonPath = clientJsonPath.replace("azuClientId", "azuClientSecret");

      if (error.contains(TENANT_ERROR)) {
        String tenantJsonPath = cloudInfoJson.get("azuTenantId").get("jsonPath").asText();
        validationErrorsMap.put(
            tenantJsonPath,
            TENANT_ERROR
                + " Check to make sure you have the correct tenant ID."
                + " This may happen if there are no active subscriptions for the tenant.");
      }
      if (error.contains(CLIENT_ERROR)) {
        validationErrorsMap.put(
            clientJsonPath,
            CLIENT_ERROR
                + ".This can happen if the application has not been installed by the administrator"
                + " of the tenant or consented to by any user in the tenant.");
      }
      if (error.contains(SECRET_ERROR)) {
        validationErrorsMap.put(secretJsonPath, SECRET_ERROR);
      }
    }

    // throw error early since it affects rest of the validation
    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
    }

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
                    manager.marketplaceAgreements().getAgreement(publisher, offer, planID);
              } catch (ManagementException e) {
                String err = String.format("Need to accept the terms for the image %s", image);
                validationErrorsMap.put(imageJsonPath, err);
              } catch (Exception e) {
                throw e;
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
    try {
      if (subscriptionID.equals(info.azuNetworkSubscriptionId)) {
        AzureCloudInfo cloudinfo =
            info.toBuilder().azuRG(resourceGroup).azuSubscriptionId(subscriptionID).build();
        AZUResourceGroupApiClient client = new AZUResourceGroupApiClient(cloudinfo);
        azure = client.getAzureResourceManager();
      }
      int regionIndex = 0;

      for (Region region : provider.getRegions()) {
        AzureRegionCloudInfo regionInfo = region.getDetails().cloudInfo.azu;
        JsonNode regionJson = regionArrayJson.get(regionIndex++);
        JsonNode cloudInfoJson = regionJson.get("details").get("cloudInfo").get("azu");
        String securityGroup = regionInfo.getSecurityGroupId();

        // verify security group exists
        if (securityGroup != null) {
          try {
            NetworkSecurityGroup azureNetworkSecurityGroup =
                azure.networkSecurityGroups().getByResourceGroup(resourceGroup, securityGroup);
          } catch (ManagementException e) {
            String sgJsonPath = cloudInfoJson.get("securityGroupId").get("jsonPath").asText();
            String err =
                String.format(
                    "Security Group: %s not found in Resource Group: %s!",
                    securityGroup, resourceGroup);
            validationErrorsMap.put(sgJsonPath, err);
          } catch (Exception e) {
            throw e;
          }
        }

        // verify virtual network exists
        String vnet = regionInfo.getVnet();
        String vnetJsonPath = cloudInfoJson.get("vnet").get("jsonPath").asText();
        try {
          Network network = azure.networks().getByResourceGroup(resourceGroup, vnet);
          // verify all subnets exist
          int zoneIndex = 0;
          ArrayNode zoneArrayJson = (ArrayNode) regionJson.get("zones");
          Map<String, Subnet> subnets = network.subnets();
          for (AvailabilityZone zone : region.getZones()) {
            String subnet = zone.getSubnet();
            String subnetJsonPath =
                zoneArrayJson.get(zoneIndex++).get("subnet").get("jsonPath").asText();
            if (!subnets.containsKey(subnet)) {
              String err =
                  String.format("Subnet %s not found under Virtual Network %s!", subnet, vnet);
              validationErrorsMap.put(subnetJsonPath, err);
            }
          }
        } catch (ManagementException e) {
          String err =
              String.format(
                  "Virtual Network: %s not found in Resource Group: %s", vnet, resourceGroup);
          validationErrorsMap.put(vnetJsonPath, err);
        } catch (Exception e) {
          throw e;
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
    } catch (ManagementException e) {
      validationErrorsMap.put(errKey, "Subscription ID not found!");
      throwMultipleProviderValidatorError(validationErrorsMap, providerJson);
    } catch (Exception e) {
      throw e;
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
        PrivateDnsZone zone =
            azure.privateDnsZones().getByResourceGroup(resourceGroup, privateDnsZone);
      } catch (ManagementException e) {
        String dnsZoneJsonPath = cloudInfoJson.get("azuHostedZoneId").get("jsonPath").asText();
        String err =
            String.format(
                "Private DNS Zone: %s not found in Resource Group: %s",
                privateDnsZone, resourceGroup);
        validationErrorsMap.put(dnsZoneJsonPath, err);
      } catch (Exception e) {
        throw e;
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
