// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.Transient;
import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@ApiModel(description = "SSL certificate used by the universe")
@Entity
@Getter
@Setter
public class CertificateInfo extends Model {

  /**
   * This is the custom certificatePath information certificates received in param are converted to
   * certs and dumped in file This contains information of file path for respective certs
   */
  public static class CustomServerCertInfo {
    public String serverCert;
    public String serverKey;

    public CustomServerCertInfo() {
      this.serverCert = null;
      this.serverKey = null;
    }

    public CustomServerCertInfo(String serverCert, String serverKey) {
      this.serverCert = serverCert;
      this.serverKey = serverKey;
    }
  }

  @ApiModelProperty(value = "Certificate UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @ApiModelProperty(
      value = "Customer UUID of the backup which it belongs to",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  @ApiModelProperty(
      value = "Certificate label",
      example = "yb-admin-example",
      accessMode = READ_WRITE)
  @Column(unique = true)
  private String label;

  @Column(nullable = false)
  @JsonIgnore
  private Date startDate;

  @ApiModelProperty(
      value = "The certificate's creation date",
      accessMode = READ_WRITE,
      example = "2022-12-12T13:07:18Z")
  // @Constraints.Required
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getStartDateIso() {
    return startDate;
  }

  public void setStartDateIso(Date startDate) {
    this.setStartDate(startDate);
  }

  @JsonIgnore
  @Column(nullable = false)
  private Date expiryDate;

  @ApiModelProperty(
      value = "The certificate's expiry date",
      accessMode = READ_WRITE,
      example = "2022-12-12T13:07:18Z")
  // @Constraints.Required
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date getExpiryDateIso() {
    return expiryDate;
  }

  @ApiModelProperty(
      value = "Private key path",
      example = "/opt/yugaware/.../example.key.pem",
      accessMode = READ_WRITE)
  @Column(nullable = true)
  private String privateKey;

  @ApiModelProperty(
      value = "Certificate path",
      example = "/opt/yugaware/certs/.../ca.root.cert",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  private String certificate;

  @ApiModelProperty(
      value = "Type of the certificate",
      example = "SelfSigned",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private CertConfigType certType;

  @ApiModelProperty(value = "The certificate file's checksum", accessMode = READ_ONLY)
  @Column(nullable = true)
  private String checksum;

  public void updateChecksum() throws IOException, NoSuchAlgorithmException {
    if (this.getCertificate() != null) {
      this.setChecksum(FileUtils.getFileChecksum(this.getCertificate()));
      this.save();
    }
  }

  @ApiModelProperty(value = "Details about the certificate", accessMode = READ_WRITE)
  @Column(columnDefinition = "TEXT", nullable = true)
  @DbJson
  // @JsonIgnore
  private JsonNode customCertInfo;

  public CertificateParams.CustomCertInfo getCustomCertPathParams() {
    if (this.getCertType() != CertConfigType.CustomCertHostPath) {
      return null;
    }
    if (this.getCustomCertInfo() != null) {
      return Json.fromJson(this.getCustomCertInfo(), CertificateParams.CustomCertInfo.class);
    }
    return null;
  }

  public void updateCustomCertPathParams(
      CertificateParams.CustomCertInfo certInfo, UUID certUUID, UUID cudtomerUUID) {
    this.checkEditable(certUUID, getCustomerUUID());
    this.setCustomCertInfo(Json.toJson(certInfo));
    this.save();
  }

  public CustomServerCertInfo getCustomServerCertInfo() {
    if (this.getCertType() != CertConfigType.CustomServerCert) {
      return null;
    }
    if (this.getCustomCertInfo() != null) {
      return Json.fromJson(this.getCustomCertInfo(), CustomServerCertInfo.class);
    }
    return null;
  }

  /**
   * To be called to return response to API calls, for java.lang.reflect
   *
   * @return
   */
  public HashicorpVaultConfigParams getCustomHCPKICertInfo() {
    if (this.getCertType() != CertConfigType.HashicorpVault) {
      return null;
    }
    if (this.getCustomCertInfo() == null) {
      return null;
    }

    HashicorpVaultConfigParams params = getCustomHCPKICertInfoInternal();
    params.vaultToken = CommonUtils.getMaskedValue("HC_VAULT_TOKEN", params.vaultToken);
    return params;
  }

  @JsonIgnore
  public HashicorpVaultConfigParams getCustomHCPKICertInfoInternal() {
    if (this.getCertType() != CertConfigType.HashicorpVault) {
      return null;
    }
    if (this.getCustomCertInfo() == null) {
      return null;
    }

    HashicorpVaultConfigParams params = new HashicorpVaultConfigParams(this.getCustomCertInfo());
    String token =
        EncryptionInTransitUtil.unmaskCertConfigData(this.getCustomerUUID(), params.vaultToken);
    if (token != null) params.vaultToken = token;

    return params;
  }

  public static final Logger LOG = LoggerFactory.getLogger(CertificateInfo.class);

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String privateKey,
      String certificate,
      CertConfigType certType)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.setUuid(uuid);
    cert.setCustomerUUID(customerUUID);
    cert.setLabel(label);
    cert.setStartDate(startDate);
    cert.setExpiryDate(expiryDate);
    cert.setPrivateKey(privateKey);
    cert.setCertificate(certificate);
    cert.setCertType(certType);
    cert.setChecksum(FileUtils.getFileChecksum(certificate));
    cert.save();
    return cert;
  }

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String certificate,
      CertificateParams.CustomCertInfo customCertInfo)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.setUuid(uuid);
    cert.setCustomerUUID(customerUUID);
    cert.setLabel(label);
    cert.setStartDate(startDate);
    cert.setExpiryDate(expiryDate);
    cert.setCertificate(certificate);
    cert.setCertType(CertConfigType.CustomCertHostPath);
    cert.setCustomCertInfo(Json.toJson(customCertInfo));
    cert.setChecksum(FileUtils.getFileChecksum(certificate));
    cert.save();
    return cert;
  }

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String certificate,
      CustomServerCertInfo customServerCertInfo)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.setUuid(uuid);
    cert.setCustomerUUID(customerUUID);
    cert.setLabel(label);
    cert.setStartDate(startDate);
    cert.setExpiryDate(expiryDate);
    cert.setCertificate(certificate);
    cert.setCertType(CertConfigType.CustomServerCert);
    cert.setCustomCertInfo(Json.toJson(customServerCertInfo));
    cert.setChecksum(FileUtils.getFileChecksum(certificate));
    cert.save();
    return cert;
  }

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String certificate,
      HashicorpVaultConfigParams params)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.setUuid(uuid);
    cert.setCustomerUUID(customerUUID);
    cert.setLabel(label);
    cert.setStartDate(startDate);
    cert.setExpiryDate(expiryDate);
    cert.setCertificate(certificate);
    cert.setCertType(CertConfigType.HashicorpVault);
    JsonNode node = params.toJsonNode();
    if (node != null) cert.setCustomCertInfo(node);
    cert.setChecksum(FileUtils.getFileChecksum(certificate));
    cert.save();
    return cert;
  }

  public static CertificateInfo createCopy(
      CertificateInfo certificateInfo, String label, String certFilePath)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo copy = new CertificateInfo();
    copy.setUuid(UUID.randomUUID());
    copy.setCustomerUUID(certificateInfo.getCustomerUUID());
    copy.setLabel(label);
    copy.setStartDate(certificateInfo.getStartDate());
    copy.setExpiryDate(certificateInfo.getExpiryDate());
    copy.setPrivateKey(certificateInfo.getPrivateKey());
    copy.setCertificate(certFilePath);
    copy.setCertType(certificateInfo.getCertType());
    copy.setChecksum(FileUtils.getFileChecksum(certFilePath));
    copy.setCustomCertInfo(certificateInfo.getCustomCertInfo());
    copy.save();
    return copy;
  }

  public CertificateInfo update(
      Date sDate, Date eDate, String certPath, HashicorpVaultConfigParams params)
      throws IOException, NoSuchAlgorithmException {

    LOG.info("Updating uuid: {} with Path:{}", getUuid().toString(), certPath);

    if (sDate != null) setStartDate(sDate);
    if (eDate != null) setExpiryDate(eDate);

    setCertificate(certPath);

    JsonNode node = params.toJsonNode();
    if (node != null) setCustomCertInfo(node);

    setChecksum(FileUtils.getFileChecksum(getCertificate()));
    save();
    return this;
  }

  public static boolean isTemporary(CertificateInfo certificateInfo) {
    return certificateInfo.getCertificate().endsWith("ca.multi.root.crt");
  }

  private static final Finder<UUID, CertificateInfo> find =
      new Finder<UUID, CertificateInfo>(CertificateInfo.class) {};

  public static CertificateInfo get(UUID certUUID) {
    return find.byId(certUUID);
  }

  public static Optional<CertificateInfo> maybeGet(UUID certUUID) {
    // Find the CertificateInfo.
    CertificateInfo certificateInfo = find.byId(certUUID);
    if (certificateInfo == null) {
      LOG.trace("Cannot find certificateInfo {}", certUUID);
      return Optional.empty();
    }
    return Optional.of(certificateInfo);
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    if (!certificateInfo.getCustomerUUID().equals(customerUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Certificate doesn't belong to customer");
    }
    return certificateInfo;
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    return certificateInfo;
  }

  public static CertificateInfo get(String label) {
    return find.query().where().eq("label", label).findOne();
  }

  public static List<CertificateInfo> getAll() {
    return find.query().where().findList();
  }

  public static CertificateInfo getOrBadRequest(String label) {
    CertificateInfo certificateInfo = get(label);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No Certificate with Label: " + label);
    }
    return certificateInfo;
  }

  public static CertificateInfo getOrBadRequest(UUID customerUUID, String label) {
    CertificateInfo certificateInfo =
        find.query().where().eq("label", label).eq("customer_uuid", customerUUID).findOne();
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No certificate with label: " + label);
    }
    return certificateInfo;
  }

  public static List<CertificateInfo> getWhereLabelStartsWith(
      String label, CertConfigType certType) {
    List<CertificateInfo> certificateInfoList =
        find.query().where().eq("cert_type", certType).like("label", label + "%").findList();
    return certificateInfoList.stream()
        .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
        .collect(Collectors.toList());
  }

  public static List<CertificateInfo> getAllNoChecksum() {
    List<CertificateInfo> certificateInfoList = find.query().where().isNull("checksum").findList();
    return certificateInfoList.stream()
        .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
        .collect(Collectors.toList());
  }

  public static List<CertificateInfo> getAll(UUID customerUUID) {
    List<CertificateInfo> certificateInfoList =
        find.query().where().eq("customer_uuid", customerUUID).findList();

    certificateInfoList =
        certificateInfoList.stream()
            .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
            .collect(Collectors.toList());

    populateUniverseData(customerUUID, certificateInfoList);
    return certificateInfoList;
  }

  public static boolean isCertificateValid(UUID certUUID) {
    if (certUUID == null) {
      return true;
    }
    CertificateInfo certificate = CertificateInfo.get(certUUID);
    if (certificate == null) {
      return false;
    }
    if (certificate.getCertType() == CertConfigType.CustomCertHostPath
        && certificate.getCustomCertInfo() == null) {
      return false;
    }
    return true;
  }

  @VisibleForTesting @Transient Boolean inUse = null;

  @ApiModelProperty(
      value =
          "Indicates whether the certificate is in use. This value is `true` if the universe"
              + " contains a reference to the certificate.",
      accessMode = READ_ONLY)
  // Returns if there is an in use reference to the object.
  public boolean getInUse() {
    if (inUse == null) {
      return Universe.existsCertificate(this.getUuid(), this.getCustomerUUID());
    } else {
      return inUse;
    }
  }

  @VisibleForTesting @Transient List<UniverseDetailSubset> universeDetailSubsets = null;

  @JsonIgnore
  public List<UniverseDetailSubset> getUniverseDetailsSubsets() {
    return universeDetailSubsets;
  }

  @ApiModelProperty(
      value = "Associated universe details for the certificate",
      accessMode = READ_ONLY)
  @JsonProperty
  public List<UniverseDetailSubset> getUniverseDetails() {
    if (universeDetailSubsets == null) {
      Set<Universe> universes =
          Universe.universeDetailsIfCertsExists(this.getUuid(), this.getCustomerUUID());
      return Util.getUniverseDetails(universes);
    } else {
      return universeDetailSubsets;
    }
  }

  @JsonIgnore
  public void setUniverseDetails(List<UniverseDetailSubset> universeDetailSubsets) {
    this.universeDetailSubsets = universeDetailSubsets;
  }

  public static void populateUniverseData(
      UUID customerUUID, List<CertificateInfo> certificateInfoList) {
    Set<Universe> universes = Customer.get(customerUUID).getUniverses();
    Set<UUID> certificateInfoSet =
        certificateInfoList.stream().map(e -> e.getUuid()).collect(Collectors.toSet());

    Map<UUID, Set<Universe>> certificateUniverseMap = new HashMap<>();
    universes.forEach(
        universe -> {
          UUID rootCA = universe.getUniverseDetails().rootCA;
          UUID clientRootCA = universe.getUniverseDetails().getClientRootCA();
          if (rootCA != null) {
            if (certificateInfoSet.contains(rootCA)) {
              certificateUniverseMap.putIfAbsent(rootCA, new HashSet<>());
              certificateUniverseMap.get(rootCA).add(universe);
            } else {
              LOG.error("Universe: {} has unknown rootCA: {}", universe.getUniverseUUID(), rootCA);
            }
          }
          if (clientRootCA != null && !clientRootCA.equals(rootCA)) {
            if (certificateInfoSet.contains(clientRootCA)) {
              certificateUniverseMap.putIfAbsent(clientRootCA, new HashSet<>());
              certificateUniverseMap.get(clientRootCA).add(universe);
            } else {
              LOG.error(
                  "Universe: {} has unknown clientRootCA: {}", universe.getUniverseUUID(), rootCA);
            }
          }
        });

    certificateInfoList.forEach(
        certificateInfo -> {
          if (certificateUniverseMap.containsKey(certificateInfo.getUuid())) {
            certificateInfo.setInUse(true);
            certificateInfo.setUniverseDetails(
                Util.getUniverseDetails(certificateUniverseMap.get(certificateInfo.getUuid())));
          } else {
            certificateInfo.setInUse(false);
            certificateInfo.setUniverseDetails(new ArrayList<>());
          }
        });
  }

  public static void delete(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificate = CertificateInfo.getOrBadRequest(certUUID, customerUUID);
    if (!certificate.getInUse()) {
      // Delete the certs from FS & DB.
      File certDirectory = new File(certificate.getCertificate()).getParentFile();
      FileData.deleteFiles(certDirectory.getAbsolutePath(), true);
      if (certificate.delete()) {
        LOG.info("Successfully deleted the certificate: " + certUUID);
      } else {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Unable to delete the Certificate");
      }
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "The certificate is in use.");
    }
  }

  public void checkEditable(UUID certUUID, UUID customerUUID) {
    CertificateInfo certInfo = getOrBadRequest(certUUID, customerUUID);
    if (certInfo.getCertType() == CertConfigType.SelfSigned) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot edit self-signed cert.");
    }
    if (!(certInfo.getCustomCertInfo() == null || certInfo.getCustomCertInfo().isNull())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit pre-customized cert. Create a new one.");
    }
  }

  public static List<CertificateInfo> getCertificateInfoList(Universe universe) {
    List<CertificateInfo> certificateInfoList = new ArrayList<CertificateInfo>();
    UUID rootCA = null;
    UUID clientRootCA = null;
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (EncryptionInTransitUtil.isRootCARequired(universeDetails)) {
      rootCA = universeDetails.rootCA;
      if (rootCA == null) {
        throw new RuntimeException(
            "No valid RootCA found for " + universeDetails.getUniverseUUID());
      }
      certificateInfoList.add(CertificateInfo.get(rootCA));
    }

    if (EncryptionInTransitUtil.isClientRootCARequired(universeDetails)) {
      clientRootCA = universeDetails.getClientRootCA();
      if (clientRootCA == null) {
        throw new RuntimeException(
            "No valid clientRootCA found for " + universeDetails.getUniverseUUID());
      }

      // check against the root to see if need to export
      if (!clientRootCA.equals(rootCA)) {
        certificateInfoList.add(CertificateInfo.get(clientRootCA));
      }
    }
    return certificateInfoList;
  }
}
