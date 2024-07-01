// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.util.Strings;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.ClientCertParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.extended.CertificateInfoExt;
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
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Certificate Info",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CertificateController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(CertificateController.class);

  @Inject private Config config;

  @Inject private RuntimeConfGetter runtimeConfGetter;

  @Inject private CertificateHelper certificateHelper;

  @ApiOperation(value = "Restore a certificate from backup", response = UUID.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "certificate",
          value = "certificate params of the backup to be restored",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CertificateParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result upload(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Form<CertificateParams> formData =
        formFactory.getFormDataOrBadRequest(request, CertificateParams.class);

    Date certStart = new Date(formData.get().certStart);
    Date certExpiry = new Date(formData.get().certExpiry);
    String label = formData.get().label;
    CertConfigType certType = formData.get().certType;
    String certContent = formData.get().certContent;
    String keyContent = formData.get().keyContent;

    CertificateParams.CustomCertInfo customCertInfo = formData.get().customCertInfo;
    CertificateParams.CustomServerCertData customServerCertData =
        formData.get().customServerCertData;
    HashicorpVaultConfigParams hcVaultParams = formData.get().hcVaultCertParams;
    checkForDuplicateCertConfig(customerUUID, label);
    switch (certType) {
      case SelfSigned:
        {
          if (certContent == null || keyContent == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Certificate or Keyfile can't be null.");
          }
          break;
        }
      case CustomCertHostPath:
        {
          if (customCertInfo == null) {
            throw new PlatformServiceException(BAD_REQUEST, "Custom Cert Info must be provided.");
          } else if (customCertInfo.nodeCertPath == null
              || customCertInfo.nodeKeyPath == null
              || customCertInfo.rootCertPath == null) {
            throw new PlatformServiceException(BAD_REQUEST, "Custom Cert Paths can't be empty.");
          }
          break;
        }
      case CustomServerCert:
        {
          if (customServerCertData == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Custom Server Cert Info must be provided.");
          } else if (customServerCertData.serverCertContent == null
              || customServerCertData.serverKeyContent == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Custom Server Cert and Key content can't be empty.");
          }
          break;
        }
      case HashicorpVault:
        {
          if (hcVaultParams == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Hashicorp Vault info must be provided.");
          }
          try {
            UUID certUUID =
                EncryptionInTransitUtil.createHashicorpCAConfig(
                    customerUUID, label, AppConfigHelper.getStoragePath(), hcVaultParams);
            auditService()
                .createAuditEntryWithReqBody(
                    request,
                    Audit.TargetType.Certificate,
                    certUUID.toString(),
                    Audit.ActionType.Create);
            return PlatformResults.withData(certUUID);

          } catch (Exception e) {
            String message = "Hashicorp Vault connection failed with exception. " + e.getMessage();
            LOG.error(message + e.getMessage());
            throw new PlatformServiceException(BAD_REQUEST, message);
          }
        }
      case K8SCertManager:
        {
          if (certContent == null) {
            throw new PlatformServiceException(BAD_REQUEST, "Certificate content is required");
          }
          if (keyContent != null) {
            throw new PlatformServiceException(BAD_REQUEST, "Only certificate is expected");
          }
          break;
        }
      default:
        {
          throw new PlatformServiceException(BAD_REQUEST, "certType should be valid.");
        }
    }
    LOG.info("CertificateController: upload cert label {}, type {}", label, certType);
    UUID certUUID =
        CertificateHelper.uploadRootCA(
            label,
            customerUUID,
            AppConfigHelper.getStoragePath(),
            certContent,
            keyContent,
            certType,
            customCertInfo,
            customServerCertData,
            runtimeConfGetter.getConfForScope(
                Customer.get(customerUUID), CustomerConfKeys.CheckCertificateConfig));
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Certificate, certUUID.toString(), Audit.ActionType.Create);
    return PlatformResults.withData(certUUID);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Create a self signed certificate",
      response = UUID.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "label",
          value = "certificate label",
          paramType = "body",
          dataType = "java.lang.String",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result createSelfSignedCert(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    ObjectNode formData = (ObjectNode) request.body().asJson();
    JsonNode jsonData = formData.get("label");
    if (jsonData == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Certificate label can't be null");
    }
    String certLabel = jsonData.asText();
    LOG.info("CertificateController: creating self signed certificate with label {}", certLabel);
    UUID certUUID =
        certificateHelper.createRootCA(runtimeConfGetter.getStaticConf(), certLabel, customerUUID);

    if (certUUID == null) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Root certificate creation failed");
    }

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Certificate,
            certUUID.toString(),
            Audit.ActionType.CreateSelfSignedCert);
    return PlatformResults.withData(certUUID);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Add a client certificate",
      response = CertificateDetails.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "certificate",
          value = "post certificate info",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.ClientCertParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result getClientCert(UUID customerUUID, UUID rootCA, Http.Request request) {
    Form<ClientCertParams> formData =
        formFactory.getFormDataOrBadRequest(request, ClientCertParams.class);
    Customer.getOrBadRequest(customerUUID);
    long certTimeMillis = formData.get().certStart;
    long certExpiryMillis = formData.get().certExpiry;
    Date certStart = certTimeMillis != 0L ? new Date(certTimeMillis) : null;
    Date certExpiry = certExpiryMillis != 0L ? new Date(certExpiryMillis) : null;

    CertificateDetails result =
        CertificateHelper.createClientCertificate(
            runtimeConfGetter.getStaticConf(),
            rootCA,
            null,
            formData.get().username,
            certStart,
            certExpiry);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Certificate,
            rootCA.toString(),
            Audit.ActionType.AddClientCertificate);
    return PlatformResults.withData(result);
  }

  // TODO: cleanup raw json
  @ApiOperation(value = "Get a customer's root certificate", response = Object.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getRootCert(UUID customerUUID, UUID rootCA, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CertificateInfo.getOrBadRequest(rootCA, customerUUID);

    try {
      CertificateInfo info = CertificateInfo.get(rootCA);

      if (info.getCertType() == CertConfigType.HashicorpVault) {
        EncryptionInTransitUtil.fetchLatestCAForHashicorpPKI(
            info, runtimeConfGetter.getStaticConf());
      }

      String certContents = CertificateHelper.getCertPEMFileContents(rootCA);
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.Certificate,
              rootCA.toString(),
              Audit.ActionType.GetRootCertificate);
      ObjectNode result = Json.newObject();
      result.put(CertificateHelper.ROOT_CERT, certContents);
      return PlatformResults.withRawData(result);
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "Failed to extract certificate");
    }
  }

  @ApiOperation(
      value = "List a customer's certificates",
      response = CertificateInfoExt.class,
      responseContainer = "List",
      nickname = "getListOfCertificate")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = 500,
          message = "If there was a server or database issue when listing the regions",
          response = YBPError.class))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    List<CertificateInfo> certs = CertificateInfo.getAll(customerUUID);
    return PlatformResults.withData(convert(certs));
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Get a certificate's UUID",
      response = UUID.class,
      nickname = "getCertificate")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUUID, String label) {
    Customer.getOrBadRequest(customerUUID);
    CertificateInfo cert = CertificateInfo.getOrBadRequest(customerUUID, label);
    return PlatformResults.withData(cert.getUuid());
  }

  @ApiOperation(
      value = "Delete a certificate",
      response = YBPSuccess.class,
      nickname = "deleteCertificate")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID reqCertUUID, Http.Request request) {
    CertificateInfo.delete(reqCertUUID, customerUUID);
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Certificate, reqCertUUID.toString(), Audit.ActionType.Delete);
    LOG.info("Successfully deleted the certificate:" + reqCertUUID);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Edit TLS certificate config details",
      response = YBPSuccess.class,
      nickname = "editCertificate")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(UUID customerUUID, UUID reqCertUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Form<CertificateParams> formData =
        formFactory.getFormDataOrBadRequest(request, CertificateParams.class);

    CertConfigType certType = formData.get().certType;
    CertificateInfo info = CertificateInfo.getOrBadRequest(reqCertUUID, customerUUID);

    if (certType != CertConfigType.HashicorpVault
        || info.getCertType() != CertConfigType.HashicorpVault) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Certificate Config does not support Edit option");
    } else {
      HashicorpVaultConfigParams formParams = formData.get().hcVaultCertParams;
      HashicorpVaultConfigParams configParams = info.getCustomHCPKICertInfoInternal();

      if (Strings.isNullOrEmpty(formParams.vaultToken)) {
        throw new PlatformServiceException(BAD_REQUEST, "Certificate Config not changed");
      }

      if (Strings.isNullOrEmpty(formParams.engine)) {
        formParams.engine = configParams.engine;
      }
      if (Strings.isNullOrEmpty(formParams.vaultAddr)) {
        formParams.vaultAddr = configParams.vaultAddr;
      }
      if (Strings.isNullOrEmpty(formParams.mountPath)) {
        formParams.mountPath = configParams.mountPath;
      }
      if (Strings.isNullOrEmpty(formParams.role)) {
        formParams.role = configParams.role;
      }

      EncryptionInTransitUtil.editEITHashicorpConfig(
          info.getUuid(), customerUUID, AppConfigHelper.getStoragePath(), formParams);
    }
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Certificate, reqCertUUID.toString(), Audit.ActionType.Edit);
    LOG.info("Successfully edited the certificate information:" + reqCertUUID);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Update an empty certificate",
      response = CertificateInfoExt.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  public Result updateEmptyCustomCert(UUID customerUUID, UUID rootCA, Http.Request request) {
    Form<CertificateParams> formData =
        formFactory.getFormDataOrBadRequest(request, CertificateParams.class);
    Customer.getOrBadRequest(customerUUID);
    CertificateInfo certificate = CertificateInfo.getOrBadRequest(rootCA, customerUUID);
    CertificateParams.CustomCertInfo customCertInfo = formData.get().customCertInfo;
    certificate.updateCustomCertPathParams(customCertInfo, rootCA, customerUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Certificate,
            Objects.toString(certificate.getUuid(), null),
            Audit.ActionType.UpdateEmptyCustomerCertificate);
    return PlatformResults.withData(convert(certificate));
  }

  private CertificateInfoExt convert(CertificateInfo certificateInfo) {
    if (certificateInfo == null) {
      return null;
    }
    return convert(Collections.singletonList(certificateInfo)).get(0);
  }

  private List<CertificateInfoExt> convert(List<CertificateInfo> certificateInfo) {
    boolean backwardCompatibleDate =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.backwardCompatibleDate);
    return certificateInfo.stream()
        .map(
            info ->
                new CertificateInfoExt()
                    .setCertificateInfo(info)
                    .setStartDate(backwardCompatibleDate ? info.getStartDate() : null)
                    .setExpiryDate(backwardCompatibleDate ? info.getExpiryDate() : null))
        .collect(Collectors.toList());
  }

  private void checkForDuplicateCertConfig(UUID customerUUID, String label) {
    CertificateInfo certificateInfo = CertificateInfo.get(customerUUID, label);
    if (certificateInfo != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Certificate with name - %s already exists", label));
    }
  }
}
