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

import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.util.Date;
import java.util.Map;

/** Provides interface to manage certificates */
public interface CertificateProviderInterface {
  /**
   * Generate/extract root CA certificate as per provider type
   *
   * @param certLabel
   * @param keyPair
   * @return X509 CA certificate
   * @throws Exception
   */
  public abstract X509Certificate generateCACertificate(String certLabel, KeyPair keyPair)
      throws Exception;

  /**
   * Generate/issue end user certificate for client/node
   *
   * @param storagePath
   * @param username
   * @param certStart
   * @param certExpiry
   * @param certFileName
   * @param certKeyName
   * @param subjectAlternateNames
   * @return Certificate Details in Strings.
   */
  public abstract CertificateDetails createCertificate(
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName,
      Map<String, Integer> subjectAltNames);
}
