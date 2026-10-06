// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.SupportBundleCreateSpecMapper;
import api.v2.mappers.SupportBundleEnumMapper;
import api.v2.mappers.SupportBundleMapper;
import api.v2.mappers.SupportBundleSizeEstimateMapper;
import api.v2.models.SupportBundle;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import api.v2.models.SupportBundlePagedQuerySpec;
import api.v2.models.SupportBundlePagedResp;
import api.v2.models.SupportBundleSizeEstimateResponse;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import api.v2.utils.NormalizedPaginationSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.SupportBundleHandlerV2;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.InputStream;
import java.util.EnumSet;
import java.util.List;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class SupportBundleManagementHandler extends ApiControllerUtils {

  private final Commissioner commissioner;
  private final SupportBundleUtil supportBundleUtil;
  private final SupportBundleHandlerV2 sbHandler;
  private final RuntimeConfGetter confGetter;

  @Inject
  public SupportBundleManagementHandler(
      AuditService auditService,
      Commissioner commissioner,
      SupportBundleUtil supportBundleUtil,
      SupportBundleHandlerV2 sbHandler,
      RuntimeConfGetter confGetter) {
    super(auditService);
    this.commissioner = commissioner;
    this.supportBundleUtil = supportBundleUtil;
    this.sbHandler = sbHandler;
    this.confGetter = confGetter;
  }

  public YBATask createSupportBundle(
      UUID customerUUID, UUID universeUUID, SupportBundleCreateSpec createSpec) {
    Customer customer = Customer.getOrNotFound(customerUUID);
    Universe universe = Universe.getOrNotFound(universeUUID, customerUUID);

    if (universe.getUniverseDetails().updateInProgress
        || universe.getUniverseDetails().universePaused) {
      log.info(
          "Trying to create support bundle while universe {} is "
              + "in a locked/paused state or has backup running.",
          universe.getName());
    }

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidation(bundleData, universe);

    SupportBundleV2 supportBundle = SupportBundleV2.create(bundleData, universe, confGetter);
    SupportBundleTaskParamsV2 taskParams =
        new SupportBundleTaskParamsV2(supportBundle, bundleData, customer, universe);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, taskParams);

    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateSupportBundleV2,
        universe.getName());
    log.info(
        "Saved task uuid {} in customer tasks table for customer: {} and universe: {}",
        taskUUID,
        customerUUID,
        universeUUID);

    return new YBATask().taskUuid(taskUUID).resourceUuid(supportBundle.getBundleUUID());
  }

  public YBATask createYbaSupportBundle(UUID customerUUID, SupportBundleCreateSpec createSpec) {
    Customer customer = Customer.getOrNotFound(customerUUID);

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidationYbaOnly(bundleData);

    SupportBundleV2 supportBundle = SupportBundleV2.createYbaOnly(bundleData, customer, confGetter);
    SupportBundleTaskParamsV2 taskParams =
        SupportBundleTaskParamsV2.forYbaOnly(supportBundle, bundleData, customer);
    UUID taskUUID = commissioner.submit(TaskType.CreateSupportBundleV2, taskParams);

    CustomerTask.create(
        customer,
        customerUUID,
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateSupportBundleV2,
        "YBA platform");
    log.info(
        "Saved task uuid {} in customer tasks table for YBA-only support bundle, customer: {}",
        taskUUID,
        customerUUID);

    return new YBATask().taskUuid(taskUUID).resourceUuid(supportBundle.getBundleUUID());
  }

  public SupportBundlePagedResp pageListSupportBundles(
      UUID customerUUID, UUID universeUUID, SupportBundlePagedQuerySpec spec) {
    Universe.getOrNotFound(universeUUID, customerUUID);
    NormalizedPaginationSpec normalized = HandlerPagingSupport.normalize(spec);
    return HandlerPagingSupport.pagedResponse(
        new SupportBundlePagedResp(),
        SupportBundleV2.getPagedList(universeUUID, normalized),
        toApi(retentionDays()));
  }

  public SupportBundlePagedResp pageListYbaSupportBundles(
      UUID customerUUID, SupportBundlePagedQuerySpec spec) {
    Customer.getOrNotFound(customerUUID);
    NormalizedPaginationSpec normalized = HandlerPagingSupport.normalize(spec);
    return HandlerPagingSupport.pagedResponse(
        new SupportBundlePagedResp(),
        SupportBundleV2.getPagedListForCustomer(customerUUID, normalized),
        toApi(retentionDays()));
  }

  public SupportBundle getSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    SupportBundleV2 bundle = universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID);
    return SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays());
  }

  public SupportBundle getYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    SupportBundleV2 bundle = ybaBundleOrNotFound(customerUUID, bundleUUID);
    return SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays());
  }

  public void deleteSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    SupportBundleV2 supportBundle =
        universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID);

    if (SupportBundleV2StatusType.Running.equals(supportBundle.getStatus())) {
      throw new PlatformServiceException(CONFLICT, "The support bundle is in running state.");
    }

    supportBundleUtil.deleteSupportBundleV2(supportBundle);

    log.info("Successfully deleted the support bundle: {}", bundleUUID);
  }

  public void deleteYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    SupportBundleV2 supportBundle = ybaBundleOrNotFound(customerUUID, bundleUUID);

    if (SupportBundleV2StatusType.Running.equals(supportBundle.getStatus())) {
      throw new PlatformServiceException(CONFLICT, "The support bundle is in running state.");
    }

    supportBundleUtil.deleteSupportBundleV2(supportBundle);

    log.info("Successfully deleted the YBA-only support bundle: {}", bundleUUID);
  }

  public InputStream downloadSupportBundle(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return toInputStream(universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID));
  }

  public InputStream downloadYbaSupportBundle(UUID customerUUID, UUID bundleUUID) {
    return toInputStream(ybaBundleOrNotFound(customerUUID, bundleUUID));
  }

  public String getDownloadFileName(UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    return universeBundleOrNotFound(customerUUID, universeUUID, bundleUUID).getFileName();
  }

  public String getYbaDownloadFileName(UUID customerUUID, UUID bundleUUID) {
    return ybaBundleOrNotFound(customerUUID, bundleUUID).getFileName();
  }

  public List<SupportBundleComponentType> listSupportBundleComponents(UUID customerUUID) {
    Customer.getOrNotFound(customerUUID);
    return EnumSet.allOf(ComponentType.class).stream()
        .map(SupportBundleEnumMapper.INSTANCE::toComponentType)
        .collect(Collectors.toList());
  }

  public SupportBundleSizeEstimateResponse estimateSupportBundleSize(
      UUID customerUUID, UUID universeUUID, SupportBundleCreateSpec createSpec) throws Exception {
    Customer customer = Customer.getOrNotFound(customerUUID);
    Universe universe = Universe.getOrNotFound(universeUUID, customerUUID);

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidation(bundleData, universe);

    return SupportBundleSizeEstimateMapper.INSTANCE.toApi(
        sbHandler.estimateBundleSize(customer, bundleData, universe));
  }

  public SupportBundleSizeEstimateResponse estimateYbaSupportBundleSize(
      UUID customerUUID, SupportBundleCreateSpec createSpec) throws Exception {
    Customer customer = Customer.getOrNotFound(customerUUID);

    SupportBundleFormDataV2 bundleData = toFormData(createSpec);
    sbHandler.bundleDataValidationYbaOnly(bundleData);

    return SupportBundleSizeEstimateMapper.INSTANCE.toApi(
        sbHandler.estimateBundleSizeYbaOnly(customer, bundleData));
  }

  /**
   * Authorization only covers the universe named in the path, so every lookup has to tie the bundle
   * back to it. Going through here rather than {@link SupportBundleV2#getOrNotFound} keeps that
   * from being forgotten.
   */
  private SupportBundleV2 universeBundleOrNotFound(
      UUID customerUUID, UUID universeUUID, UUID bundleUUID) {
    Universe.getOrNotFound(universeUUID, customerUUID);
    SupportBundleV2 bundle = SupportBundleV2.getOrNotFound(bundleUUID);
    bundle.verifyUniverseScope(universeUUID);
    return bundle;
  }

  private SupportBundleV2 ybaBundleOrNotFound(UUID customerUUID, UUID bundleUUID) {
    Customer.getOrNotFound(customerUUID);
    SupportBundleV2 bundle = SupportBundleV2.getOrNotFound(bundleUUID);
    bundle.verifyCustomerScope(customerUUID);
    return bundle;
  }

  private SupportBundleFormDataV2 toFormData(SupportBundleCreateSpec createSpec) {
    SupportBundleFormDataV2 bundleData =
        SupportBundleCreateSpecMapper.INSTANCE.toSupportBundleFormData(createSpec);
    bundleData.resolveDefaultDates(confGetter);
    return bundleData;
  }

  /** Retention days is read once per request rather than per row of the page. */
  private Function<SupportBundleV2, SupportBundle> toApi(int retentionDays) {
    return bundle -> SupportBundleMapper.INSTANCE.toApi(bundle, retentionDays);
  }

  private InputStream toInputStream(SupportBundleV2 bundle) {
    if (bundle.getStatus() != SupportBundleV2StatusType.Success) {
      throw new PlatformServiceException(
          NOT_FOUND, String.format("No bundle found for %s", bundle.getBundleUUID()));
    }
    return SupportBundleV2.getAsInputStream(bundle.getBundleUUID());
  }

  private int retentionDays() {
    return confGetter.getStaticConf().getInt("yb.support_bundle.retention_days");
  }
}
