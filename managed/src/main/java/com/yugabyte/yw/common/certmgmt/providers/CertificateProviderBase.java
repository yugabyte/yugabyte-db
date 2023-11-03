/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.certmgmt.providers;

import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.tuple.Pair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** common impl for certificates provider handling */
public abstract class CertificateProviderBase implements CertificateProviderInterface {
  public static final Logger LOG = LoggerFactory.getLogger(CertificateProviderBase.class);

  CertConfigType certConfigType;
  public UUID caCertUUID;

  public CertificateProviderBase(CertConfigType type, UUID pCACertUUID) {
    certConfigType = type;
    caCertUUID = pCACertUUID;
  }

  @Override
  public X509Certificate generateCACertificate(String certLabel, KeyPair keyPair) throws Exception {
    throw new RuntimeException("Invalid usage, calling CertificateProviderBase method");
  }

  @Override
  public CertificateDetails createCertificate(
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName,
      Map<String, Integer> subjectAltNames) {
    throw new RuntimeException("Invalid usage, calling CertificateProviderBase method");
  }

  public abstract Pair<String, String> dumpCACertBundle(
      String storagePath, UUID customerUUID, UUID caCertUUIDParam) throws Exception;

  public Pair<String, String> dumpCACertBundle(String storagePath, UUID customerUUID)
      throws Exception {
    return dumpCACertBundle(storagePath, customerUUID, caCertUUID);
  }
}
