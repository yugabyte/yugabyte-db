package com.yugabyte.yw.cloud.azu;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.credential.TokenCredential;
import com.azure.core.http.policy.ExponentialBackoffOptions;
import com.azure.core.http.policy.RetryOptions;
import com.azure.core.http.rest.PagedIterable;
import com.azure.core.management.AzureEnvironment;
import com.azure.core.management.SubResource;
import com.azure.core.management.exception.ManagementException;
import com.azure.core.management.profile.AzureProfile;
import com.azure.core.util.Context;
import com.azure.resourcemanager.AzureResourceManager;
import com.azure.resourcemanager.compute.fluent.ComputeManagementClient;
import com.azure.resourcemanager.compute.fluent.models.CapacityReservationGroupInner;
import com.azure.resourcemanager.compute.fluent.models.CapacityReservationInner;
import com.azure.resourcemanager.compute.fluent.models.VirtualMachineInner;
import com.azure.resourcemanager.compute.models.ApiErrorException;
import com.azure.resourcemanager.compute.models.CapacityReservationUpdate;
import com.azure.resourcemanager.compute.models.Sku;
import com.azure.resourcemanager.compute.models.VirtualMachine;
import com.azure.resourcemanager.marketplaceordering.MarketplaceOrderingManager;
import com.azure.resourcemanager.network.fluent.models.BackendAddressPoolInner;
import com.azure.resourcemanager.network.fluent.models.LoadBalancerInner;
import com.azure.resourcemanager.network.fluent.models.NetworkInterfaceInner;
import com.azure.resourcemanager.network.models.LoadBalancerBackendAddress;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RetryTaskUntilCondition;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Data
public class AZUResourceGroupApiClient {
  private static final String ERROR_202 = "Status code 202, (empty body)";
  private static final int GROUP_DELETE_WAIT_RETRIES = 5;
  private static final int GROUP_DELETE_WAIT_SECONDS = 60;

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

  public String createCapacityReservationGroup(
      String groupName, String region, Set<String> zones, Map<String, String> tags) {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    CapacityReservationGroupInner parameters = new CapacityReservationGroupInner();
    parameters.withLocation(region).withZones(new ArrayList<>(zones)).withTags(tags);

    CapacityReservationGroupInner group =
        client.getCapacityReservationGroups().createOrUpdate(resourceGroup, groupName, parameters);
    return group.id();
  }

  public Set<String> listCapacityReservationGroups() {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    PagedIterable<CapacityReservationGroupInner> capacityReservationGroupInners =
        client.getCapacityReservationGroups().listByResourceGroup(resourceGroup);
    Set<String> result = new HashSet<>();
    for (CapacityReservationGroupInner capacityReservationGroupInner :
        capacityReservationGroupInners) {
      result.add(capacityReservationGroupInner.name());
    }
    return result;
  }

  public void deleteCapacityReservationGroup(String groupName) {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    try {
      client.getCapacityReservationGroups().delete(resourceGroup, groupName);
    } catch (Exception ex) {
      log.error("Failed to delete", ex);
      // Azure returns 202 but delete is successful
      if (ex instanceof ApiErrorException && ex.getLocalizedMessage().equals(ERROR_202)) {
        log.debug("Got 202, polling for result");
        RetryTaskUntilCondition<Set<String>> retry =
            new RetryTaskUntilCondition<>(
                this::listCapacityReservationGroups, (groups) -> !groups.contains(groupName));
        boolean deleted =
            retry.retryUntilCond(
                GROUP_DELETE_WAIT_SECONDS, GROUP_DELETE_WAIT_SECONDS * GROUP_DELETE_WAIT_RETRIES);
        if (deleted) {
          return;
        }
      }
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete group: " + ex.getMessage());
    }
  }

  public Set<String> listCapacityReservations(String groupName) {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    PagedIterable<CapacityReservationInner> capacityReservationInners =
        client.getCapacityReservations().listByCapacityReservationGroup(resourceGroup, groupName);
    Set<String> result = new HashSet<>();
    for (CapacityReservationInner capacityReservationInner : capacityReservationInners) {
      result.add(capacityReservationInner.name());
    }
    return result;
  }

  public void deleteCapacityReservation(String groupName, String reservationName, Set<String> vms) {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    log.debug("Delete reservation {}", reservationName);
    CapacityReservationInner reservation =
        client.getCapacityReservations().get(resourceGroup, groupName, reservationName);
    if (reservation.sku().capacity() > 0
        && !reservation.provisioningState().equalsIgnoreCase("Failed")) {
      try {
        client
            .getCapacityReservations()
            .update(
                resourceGroup,
                groupName,
                reservationName,
                new CapacityReservationUpdate().withSku(new Sku().withCapacity(0L)));
      } catch (ManagementException e) {
        log.error("Failed to update reservation to 0", e);
        throw new RuntimeException(e.getValue().getMessage());
      }
    }
    for (String vm : vms) {
      try {
        VirtualMachine byId =
            azureResourceManager.virtualMachines().getByResourceGroup(resourceGroup, vm);
        if (byId.capacityReservationGroupId() != null) {
          byId.update().withCapacityReservationGroup(null).apply();
        }
      } catch (Exception e) {
        log.error("Failed to load vm " + vm, e);
      }
    }
    try {
      client.getCapacityReservations().delete(resourceGroup, groupName, reservationName);
    } catch (ManagementException e) {
      log.error("Failed to delete reservation", e);
      throw new RuntimeException(e.getValue().getMessage());
    }
  }

  public String createCapacityReservation(
      String groupName,
      String region,
      String zone,
      String reservationName,
      String instanceType,
      Integer count,
      Map<String, String> tags) {
    ComputeManagementClient client = azureResourceManager.computeSkus().manager().serviceClient();
    CapacityReservationInner params =
        new CapacityReservationInner()
            .withLocation(region)
            .withZones(Collections.singletonList(zone))
            .withTags(tags)
            .withSku(new Sku().withCapacity(count.longValue()).withName(instanceType));
    try {
      CapacityReservationInner reservation =
          client
              .getCapacityReservations()
              .createOrUpdate(resourceGroup, groupName, reservationName, params);
    } catch (ManagementException e) {
      log.error("Failed to create reservation", e);
      if (e.getValue().getMessage().contains("Capacity Reservation is not supported")) {
        return null;
      }
      throw new RuntimeException(e.getValue().getMessage());
    }

    return reservationName;
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
