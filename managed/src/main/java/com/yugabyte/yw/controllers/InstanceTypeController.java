// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static java.util.function.Function.identity;
import static java.util.stream.Collectors.groupingBy;
import static java.util.stream.Collectors.mapping;
import static java.util.stream.Collectors.toMap;
import static java.util.stream.Collectors.toSet;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.annotations.BlockOperatorResource;
import com.yugabyte.yw.common.operator.annotations.OperatorResourceTypes;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.InstanceTypeResp;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Instance types",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class InstanceTypeController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(InstanceTypeController.class);
  private final Config config;
  private final CloudAPI.Factory cloudAPIFactory;

  // TODO: Remove this when we have HelperMethod in place to get Config details
  @Inject
  public InstanceTypeController(Config config, CloudAPI.Factory cloudAPIFactory) {
    this.config = config;
    this.cloudAPIFactory = cloudAPIFactory;
  }

  @Inject RuntimeConfGetter confGetter;

  /**
   * GET endpoint for listing instance types
   *
   * @param customerUUID, UUID of customer
   * @param providerUUID, UUID of provider
   * @return JSON response with instance types
   */
  @ApiOperation(
      value = "List a provider's instance types",
      response = InstanceTypeResp.class,
      responseContainer = "List",
      nickname = "listOfInstanceType")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the instance types",
          response = YBPError.class))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(
      UUID customerUUID, UUID providerUUID, List<String> zoneCodes, @Nullable String arch) {
    Set<String> filterByZoneCodes = new HashSet<>(zoneCodes);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    if (arch == null && confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
      // This will be the case of legacy flow, where we don't have architecture as top level
      // universe property. We can retrieve the arch from the default Image bundle for the provider.
      List<ImageBundle> defaultImageBundles = ImageBundle.getDefaultForProvider(providerUUID);
      if (defaultImageBundles.size() > 0) {
        arch = defaultImageBundles.get(0).getDetails().getArch().toString();
      }
    }
    final String architecture = arch;
    Map<String, InstanceType> instanceTypesMap;
    instanceTypesMap =
        InstanceType.findByProvider(
                provider,
                confGetter,
                confGetter.getConfForScope(provider, ProviderConfKeys.allowUnsupportedInstances))
            .stream()
            .filter(
                it -> {
                  if (provider.getCloudCode() == CloudType.aws && architecture != null) {
                    return it.getInstanceTypeDetails().arch == Architecture.valueOf(architecture);
                  }
                  return true;
                })
            .collect(toMap(it -> it.getInstanceTypeCode(), identity()));

    return maybeFilterByZoneOfferings(filterByZoneCodes, provider, instanceTypesMap);
  }

  private Result maybeFilterByZoneOfferings(
      Set<String> filterByZoneCodes,
      Provider provider,
      Map<String, InstanceType> instanceTypesMap) {
    if (filterByZoneCodes.isEmpty()) {
      LOG.debug("No zones specified. Skipping filtering by zone.");
    } else {
      CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCode());
      if (cloudAPI != null) {
        try {
          LOG.debug(
              "Full list of instance types: {}. Filtering it based on offerings.",
              instanceTypesMap.keySet());
          Map<Region, Set<String>> azByRegionMap =
              filterByZoneCodes.stream()
                  .map(code -> AvailabilityZone.getByCode(provider, code))
                  .collect(groupingBy(az -> az.getRegion(), mapping(az -> az.getCode(), toSet())));

          LOG.debug("AZs looked up from db {}", azByRegionMap);

          Map<String, Set<String>> offeringsByInstanceType =
              cloudAPI.offeredZonesByInstanceType(
                  provider, azByRegionMap, instanceTypesMap.keySet());

          LOG.debug("Instance Type Offerings from cloud: {}.", offeringsByInstanceType);

          List<InstanceType> filteredInstanceTypes =
              offeringsByInstanceType.entrySet().stream()
                  .filter(kv -> kv.getValue().size() >= filterByZoneCodes.size())
                  .map(Map.Entry::getKey)
                  .map(instanceTypesMap::get)
                  .collect(Collectors.toList());

          LOG.info(
              "Num instanceTypes excluded {} because they were not offered in selected AZs.",
              instanceTypesMap.size() - filteredInstanceTypes.size());

          return PlatformResults.withData(convert(filteredInstanceTypes, provider));
        } catch (Exception exception) {
          LOG.warn(
              "There was an error {} talking to {} cloud API or filtering instance types "
                  + "based on per zone offerings for user selected zones: {}. We won't filter.",
              exception,
              provider.getCode(),
              filterByZoneCodes);
        }
      } else {
        LOG.info("No Cloud API defined for {}. Skipping filtering by zone.", provider.getCode());
      }
    }
    return PlatformResults.withData(convert(instanceTypesMap.values(), provider));
  }

  private InstanceTypeResp convert(InstanceType it, Provider provider) {
    return new InstanceTypeResp()
        .setInstanceType(it)
        .setProviderCode(provider.getCode())
        .setProviderUuid(provider.getUuid());
  }

  private List<InstanceTypeResp> convert(
      Collection<InstanceType> instanceTypes, Provider provider) {
    return instanceTypes.stream().map(it -> convert(it, provider)).collect(Collectors.toList());
  }

  /**
   * POST endpoint for creating new instance type
   *
   * @param customerUUID, UUID of customer
   * @param providerUUID, UUID of provider
   * @return JSON response of newly created instance type
   */
  @ApiOperation(
      value = "Create an instance type",
      response = InstanceTypeResp.class,
      nickname = "createInstanceType")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "Instance type",
          value = "Instance type data of the instance to be stored",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.InstanceType",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.PROVIDER)
  public Result create(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Form<InstanceType> formData = formFactory.getFormDataOrBadRequest(request, InstanceType.class);

    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    if (provider.getCloudCode() == CloudType.aws
        && confGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
      // Check in case the arch is specified for the instance.
      InstanceTypeDetails instanceDetails = formData.get().getInstanceTypeDetails();
      if (instanceDetails == null || instanceDetails.arch == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Please specify the architecture for the instance type %s",
                formData.get().getInstanceTypeCode()));
      }
    }
    InstanceType it =
        InstanceType.upsert(
            provider.getUuid(),
            formData.get().getInstanceTypeCode(),
            formData.get().getNumCores(),
            formData.get().getMemSizeGB(),
            formData.get().getInstanceTypeDetails());
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.CreateInstanceType);
    return PlatformResults.withData(convert(it, provider));
  }

  /**
   * DELETE endpoint for deleting instance types.
   *
   * @param customerUUID, UUID of customer
   * @param providerUUID, UUID of provider
   * @param instanceTypeCode, Instance type code.
   * @return JSON response to denote if the delete was successful or not.
   */
  @ApiOperation(
      value = "Delete an instance type",
      response = YBPSuccess.class,
      nickname = "deleteInstanceType")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.PROVIDER)
  public Result delete(
      UUID customerUUID, UUID providerUUID, String instanceTypeCode, Http.Request request) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    InstanceType instanceType = InstanceType.getOrBadRequest(provider.getUuid(), instanceTypeCode);
    if (!NodeInstance.getByInstanceType(providerUUID, instanceTypeCode).isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete the Instance Type with existing Instances");
    }
    instanceType.setActive(false);
    instanceType.save();
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.DeleteInstanceType);
    return YBPSuccess.empty();
  }

  /**
   * Info endpoint for getting instance type information.
   *
   * @param customerUUID, UUID of customer
   * @param providerUUID, UUID of provider.
   * @param instanceTypeCode, Instance type code.
   * @return JSON response with instance type information.
   */
  @ApiOperation(
      value = "Get details of an instance type",
      response = InstanceTypeResp.class,
      nickname = "instanceTypeDetail")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result index(UUID customerUUID, UUID providerUUID, String instanceTypeCode) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);

    InstanceType instanceType = InstanceType.getOrBadRequest(provider.getUuid(), instanceTypeCode);
    // Mount paths are not persisted for non-onprem clouds, but we know the default details.
    if (!provider.getCode().equals(onprem.toString())) {
      instanceType.getInstanceTypeDetails().setDefaultMountPaths();
    }
    return PlatformResults.withData(convert(instanceType, provider));
  }

  /**
   * Metadata endpoint for getting a list of all supported types of EBS volumes.
   *
   * @return a list of all supported types of EBS volumes.
   */
  @ApiOperation(
      value = "List supported EBS volume types",
      response = StorageType.class,
      responseContainer = "List")
  @AuthzPath
  public Result getEBSTypes() {
    return PlatformResults.withData(
        Arrays.stream(PublicCloudConstants.StorageType.values())
            .filter(name -> name.getCloudType().equals(Common.CloudType.aws))
            .toArray());
  }

  /**
   * Metadata endpoint for getting a list of all supported types of GCP disks.
   *
   * @return a list of all supported types of GCP disks.
   */
  @ApiOperation(
      value = "List supported GCP disk types",
      response = StorageType.class,
      responseContainer = "List")
  @AuthzPath
  public Result getGCPTypes() {

    return PlatformResults.withData(
        Arrays.stream(PublicCloudConstants.StorageType.values())
            .filter(name -> name.getCloudType().equals(Common.CloudType.gcp))
            .toArray());
  }

  /**
   * Metadata endpoint for getting a list of all supported types of AZU disks.
   *
   * @return a list of all supported types of AZU disks.
   */
  @ApiOperation(
      value = "List supported Azure disk types",
      response = StorageType.class,
      responseContainer = "List")
  @AuthzPath
  public Result getAZUTypes() {
    return PlatformResults.withData(
        Arrays.stream(PublicCloudConstants.StorageType.values())
            .filter(name -> name.getCloudType().equals(Common.CloudType.azu))
            .toArray());
  }
}
