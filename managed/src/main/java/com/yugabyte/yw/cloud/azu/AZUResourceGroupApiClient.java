package com.yugabyte.yw.cloud.azu;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.credential.TokenCredential;
import com.azure.core.http.policy.ExponentialBackoffOptions;
import com.azure.core.http.policy.RetryOptions;
import com.azure.core.management.AzureEnvironment;
import com.azure.core.management.SubResource;
import com.azure.core.management.profile.AzureProfile;
import com.azure.core.util.Context;
import com.azure.resourcemanager.AzureResourceManager;
import com.azure.resourcemanager.compute.fluent.models.VirtualMachineInner;
import com.azure.resourcemanager.marketplaceordering.MarketplaceOrderingManager;
import com.azure.resourcemanager.network.fluent.models.BackendAddressPoolInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancerInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceInner;
import com.azure.resourcemanager.network.models.LoadBalancerBackendAddress;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Data
public class AZUResourceGroupApiClient {

  private AzureResourceManager azureResourceManager;
  private String resourceGroup;
  private MarketplaceOrderingManager azureMarketplaceOrderingManager;

  public AZUResourceGroupApiClient(AzureCloudInfo azureCloudInfo) {
    this.resourceGroup = azureCloudInfo.getAzuRG();
    this.azureResourceManager = getResourceManager(azureCloudInfo);
    this.azureMarketplaceOrderingManager = getAzureMarketplaceOrderingManager(azureCloudInfo);
  }

  public BackendAddressPoolInner createNewBackendPoolForIPs(
      String lbName, Map<String, String> ipToVmName, SubResource virtualNetwork) {
    String backendPoolName = "bp-" + UUID.randomUUID().toString();
    BackendAddressPoolInner backendPool = new BackendAddressPoolInner().withName(backendPoolName);
    return updateIPsInBackendPool(lbName, ipToVmName, backendPool, virtualNetwork);
  }

  public BackendAddressPoolInner updateIPsInBackendPool(
      String lbName,
      Map<String, String> ipToVmName,
      BackendAddressPoolInner backendAddressPool,
      SubResource virtualNetwork) {
    List<LoadBalancerBackendAddress> loadBalancerAddresses =
        ipToVmName.entrySet().stream()
            .map(
                ipToName ->
                    (new LoadBalancerBackendAddress())
                        .withName(ipToName.getValue())
                        .withIpAddress(ipToName.getKey())
                        .withVirtualNetwork(virtualNetwork))
            .collect(Collectors.toList());
    backendAddressPool = backendAddressPool.withLoadBalancerBackendAddresses(loadBalancerAddresses);
    return azureResourceManager
        .networks()
        .manager()
        .serviceClient()
        .getLoadBalancerBackendAddressPools()
        .createOrUpdate(
            resourceGroup, lbName, backendAddressPool.name(), backendAddressPool, Context.NONE);
  }

  public LoadBalancerInner updateLoadBalancer(String lbName, LoadBalancerInner loadBalancer) {
    return azureResourceManager
        .networks()
        .manager()
        .serviceClient()
        .getLoadBalancers()
        .createOrUpdate(resourceGroup, lbName, loadBalancer);
  }

  public LoadBalancerInner getLoadBalancerByName(String lbName) {
    LoadBalancerInner loadBalancer =
        azureResourceManager
            .networks()
            .manager()
            .serviceClient()
            .getLoadBalancers()
            .getByResourceGroup(resourceGroup, lbName);
    if (loadBalancer == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot find Load Balancer with given name: " + lbName);
    }
    return loadBalancer;
  }

  public VirtualMachineInner getVirtulMachineDetailsByName(String vmName) {
    VirtualMachineInner vm =
        azureResourceManager
            .virtualMachines()
            .manager()
            .serviceClient()
            .getVirtualMachines()
            .getByResourceGroup(resourceGroup, vmName);
    if (vm == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Cannot find VM with name: " + vmName);
    }
    return vm;
  }

  public NetworkInterfaceInner getNetworkInterfaceByName(String networkInterfaceName) {
    NetworkInterfaceInner networkInterface =
        azureResourceManager
            .networks()
            .manager()
            .serviceClient()
            .getNetworkInterfaces()
            .getByResourceGroup(resourceGroup, networkInterfaceName);
    if (networkInterface == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Cannot find network interface with name: " + networkInterfaceName);
    }
    return networkInterface;
  }

  private AzureResourceManager getResourceManager(AzureCloudInfo azCloudInfo) {
    // azure sdk generally has a retry count of 3 so keeping that default
    return getResourceManager(azCloudInfo, 3);
  }

  public AzureResourceManager getResourceManager(AzureCloudInfo azCloudInfo, int retryCount) {
    AzureProfile azureProfile =
        new AzureProfile(
            azCloudInfo.getAzuTenantId(),
            azCloudInfo.getAzuSubscriptionId(),
            AzureEnvironment.AZURE);
    TokenCredential credential = AZUCloudImpl.getCredsOrFallbackToDefault(azCloudInfo);
    AzureResourceManager.Authenticated authenticated =
        AzureResourceManager.configure()
            .withRetryOptions(
                new RetryOptions(new ExponentialBackoffOptions().setMaxRetries(retryCount)))
            .authenticate(credential, azureProfile);
    AzureResourceManager azure = authenticated.withSubscription(azCloudInfo.getAzuSubscriptionId());
    return azure;
  }

  private MarketplaceOrderingManager getAzureMarketplaceOrderingManager(
      AzureCloudInfo azCloudInfo) {
    AzureProfile azureProfile =
        new AzureProfile(
            azCloudInfo.getAzuTenantId(),
            azCloudInfo.getAzuSubscriptionId(),
            AzureEnvironment.AZURE);
    TokenCredential credential = AZUCloudImpl.getCredsOrFallbackToDefault(azCloudInfo);
    MarketplaceOrderingManager manager =
        MarketplaceOrderingManager.authenticate(credential, azureProfile);
    return manager;
  }
}
