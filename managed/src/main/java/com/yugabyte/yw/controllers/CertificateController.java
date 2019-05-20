package com.yugabyte.yw.controllers;

import com.google.inject.Inject;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.forms.CertificateParams;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;
import play.data.Form;
import play.data.FormFactory;

import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

public class CertificateController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CertificateController.class);

  @Inject
  play.Configuration appConfig;

  @Inject
  FormFactory formFactory;

  public Result upload(UUID customerUUID) {
    Form<CertificateParams> formData = formFactory.form(CertificateParams.class)
                                                  .bindFromRequest();
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    Date certStart = new Date(formData.get().certStart);
    Date certExpiry = new Date(formData.get().certExpiry);
    String label = formData.get().label;
    String certContent = formData.get().certContent;
    String keyContent = formData.get().keyContent;
    try {
      UUID certUUID = CertificateHelper.uploadRootCA(label, customerUUID, appConfig.getString("yb.storage.path"),
          certContent, keyContent, certStart, certExpiry);
      return ApiResponse.success(certUUID);
    } catch (Exception e) {
      return ApiResponse.error(BAD_REQUEST, "Couldn't upload certfiles");
    }

  }

  public Result list(UUID customerUUID) {
    List<CertificateInfo> certs = CertificateInfo.getAll(customerUUID);
    if (certs == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      List<String> cert_labels = CertificateInfo.getAll(customerUUID)
                                                .stream().map(cert->cert.label)
                                                .collect(Collectors.toList());
      return ApiResponse.success(cert_labels);
    } catch (RuntimeException re) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, re.getMessage());
    }
  }

  public Result get(UUID customerUUID, String label) {
    CertificateInfo cert = CertificateInfo.get(label);
    if (cert == null) {
      return ApiResponse.error(BAD_REQUEST, "No Certificate with Label: " + label);
    } else {
      return ApiResponse.success(cert.uuid);
    }
  }
}
