package com.yugabyte.yw.controllers;

import com.yugabyte.yw.models.CertificateInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import com.yugabyte.yw.common.ApiResponse;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

public class CertificateController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CertificateController.class);

  public Result list(UUID customerUUID) {
    List<CertificateInfo> certs = CertificateInfo.getAll(customerUUID);
    if (certs == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      List<String> cert_labels = CertificateInfo.getAll(customerUUID).stream().map(cert->cert.label).collect(Collectors.toList());
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
