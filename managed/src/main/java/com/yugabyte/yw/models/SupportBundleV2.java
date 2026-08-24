// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.handlers.HandlerPagingSupport;
import api.v2.utils.NormalizedPaginationSpec;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.helpers.BundleDetails;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PagedList;
import io.ebean.annotation.DbJson;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.Transient;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
@Table(name = "support_bundle_v2")
public class SupportBundleV2 extends Model {

  private static final Logger LOG = LoggerFactory.getLogger(SupportBundleV2.class);

  @Id
  @Column(nullable = false, unique = true)
  @Getter
  UUID bundleUUID;

  @Column @Getter @Setter String path;

  @Column(nullable = true)
  @Getter
  UUID scopeUUID;

  @Column(nullable = true)
  @Getter
  UUID customerUUID;

  @Column @Getter @Setter @JsonIgnore Date creationDate;

  @Column
  @Getter
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date startDate;

  @Column
  @Getter
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date endDate;

  @Column(nullable = true)
  @Getter
  @DbJson
  BundleDetails bundleDetails;

  @Column(name = "status", nullable = false)
  @Getter
  @Setter
  SupportBundleV2StatusType status;

  @Transient private long sizeInBytes;

  public SupportBundleV2() {}

  @JsonIgnore
  public Path getPathObject() {
    if (this.path == null) {
      return null;
    }
    return Paths.get(this.path);
  }

  public void setPathObject(Path path) {
    this.path = path.toString();
  }

  public static SupportBundleV2 createYbaOnly(
      SupportBundleFormDataV2 bundleData, Customer customer, RuntimeConfGetter confGetter) {
    SupportBundleV2 supportBundle = new SupportBundleV2();
    supportBundle.bundleUUID = UUID.randomUUID();
    supportBundle.customerUUID = customer.getUuid();
    supportBundle.scopeUUID = null;
    supportBundle.path = null;
    supportBundle.creationDate = new Date();
    populateFromFormData(supportBundle, bundleData, confGetter);
    supportBundle.status = SupportBundleV2StatusType.Running;
    supportBundle.save();
    return supportBundle;
  }

  public static SupportBundleV2 create(
      SupportBundleFormDataV2 bundleData, Universe universe, RuntimeConfGetter confGetter) {
    SupportBundleV2 supportBundle = new SupportBundleV2();
    supportBundle.bundleUUID = UUID.randomUUID();
    supportBundle.scopeUUID = universe.getUniverseUUID();
    supportBundle.path = null;
    supportBundle.creationDate = new Date();
    populateFromFormData(supportBundle, bundleData, confGetter);
    supportBundle.status = SupportBundleV2StatusType.Running;
    supportBundle.save();
    return supportBundle;
  }

  private static void populateFromFormData(
      SupportBundleV2 supportBundle,
      SupportBundleFormDataV2 bundleData,
      RuntimeConfGetter confGetter) {
    if (bundleData == null) {
      return;
    }
    supportBundle.startDate = bundleData.startDate;
    supportBundle.endDate = bundleData.endDate;
    supportBundle.bundleDetails = buildBundleDetails(bundleData, confGetter);
  }

  private static BundleDetails buildBundleDetails(
      SupportBundleFormDataV2 bundleData, RuntimeConfGetter confGetter) {
    BundleDetails bundleDetails =
        new BundleDetails(
            bundleData.components,
            bundleData.maxNumRecentCores,
            bundleData.maxCoreFileSize,
            bundleData.promDumpStartDate,
            bundleData.promDumpEndDate,
            bundleData.promMetricsFormat,
            bundleData.promDumpDownSample
                ? (bundleData.stepPromDumpSecs != null
                    ? bundleData.stepPromDumpSecs
                    : confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
                : (int) SwamperHelper.getScrapeIntervalSeconds(confGetter.getStaticConf()),
            bundleData.prometheusMetricsTypes,
            bundleData.paDumpStartDate,
            bundleData.paDumpEndDate,
            bundleData.paMetricsFormat);
    bundleDetails.setNodeNames(bundleData.nodeNames);
    bundleDetails.setFilesComponentSpecs(bundleData.filesComponentSpecs);
    bundleDetails.setBashComponentSpecs(bundleData.bashComponentSpecs);
    bundleDetails.setYsqlComponentSpecs(bundleData.ysqlComponentSpecs);
    bundleDetails.setYcqlComponentSpecs(bundleData.ycqlComponentSpecs);
    bundleDetails.setYbAdminComponentSpecs(bundleData.ybAdminComponentSpecs);
    bundleDetails.setYbaComponentSpecs(bundleData.ybaComponentSpecs);
    return bundleDetails;
  }

  public static final Finder<UUID, SupportBundleV2> find =
      new Finder<UUID, SupportBundleV2>(SupportBundleV2.class) {};

  public static SupportBundleV2 getOrNotFound(UUID bundleUUID) {
    SupportBundleV2 bundle = get(bundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(NOT_FOUND, "Bundle not found: " + bundleUUID);
    }
    return bundle;
  }

  public static SupportBundleV2 get(UUID bundleUUID) {
    return find.query().where().eq("bundle_uuid", bundleUUID).findOne();
  }

  public static List<SupportBundleV2> getAll() {
    return find.query().findList();
  }

  public static InputStream getAsInputStream(UUID bundleUUID) {
    SupportBundleV2 supportBundle = getOrNotFound(bundleUUID);
    Path bundlePath = supportBundle.getPathObject();
    File file = bundlePath.toFile();
    return FileUtils.getInputStreamOrFail(file);
  }

  @JsonIgnore
  public String getFileName() {
    Path bundlePath = this.getPathObject();
    if (bundlePath == null) {
      return null;
    }
    return bundlePath.getFileName().toString();
  }

  @JsonIgnore
  public long getSizeInBytes() {
    if (this.status != SupportBundleV2StatusType.Success) {
      return 0;
    }

    sizeInBytes = FileUtils.getFileSize(path);
    return sizeInBytes;
  }

  public static List<SupportBundleV2> getAll(UUID universeUUID) {
    return find.query().where().eq("scope_uuid", universeUUID).findList();
  }

  public static PagedList<SupportBundleV2> getPagedList(
      UUID universeUUID, NormalizedPaginationSpec normalized) {
    return getPagedList(find.query().where().eq("scope_uuid", universeUUID), normalized);
  }

  public static PagedList<SupportBundleV2> getPagedListForCustomer(
      UUID customerUUID, NormalizedPaginationSpec normalized) {
    return getPagedList(find.query().where().eq("customer_uuid", customerUUID), normalized);
  }

  /**
   * Bundle UUID breaks ties so that rows created within the same instant page deterministically.
   */
  private static PagedList<SupportBundleV2> getPagedList(
      ExpressionList<SupportBundleV2> expr, NormalizedPaginationSpec normalized) {
    String order = normalized.order();
    String orderBy = String.format("creation_date %s, bundle_uuid %s", order, order);
    return HandlerPagingSupport.getPagedList(expr, normalized, orderBy);
  }

  public void verifyCustomerScope(UUID customerUUID) {
    if (customerUUID == null || !customerUUID.equals(this.customerUUID)) {
      throw new PlatformServiceException(
          NOT_FOUND, "Support bundle does not belong to customer " + customerUUID);
    }
  }

  public void verifyUniverseScope(UUID universeUUID) {
    if (universeUUID == null || !universeUUID.equals(this.scopeUUID)) {
      throw new PlatformServiceException(
          NOT_FOUND, "Support bundle does not belong to universe " + universeUUID);
    }
  }

  public boolean isYbaOnly() {
    return customerUUID != null && scopeUUID == null;
  }

  public static void delete(UUID bundleUUID) {
    SupportBundleV2 supportBundle = SupportBundleV2.getOrNotFound(bundleUUID);
    if (supportBundle.delete()) {
      LOG.info("Successfully deleted the db entry for support bundle v2: {}", bundleUUID);
    } else {
      throw new PlatformServiceException(NOT_FOUND, "Bundle not found: " + bundleUUID);
    }
  }
}
