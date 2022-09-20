/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use info file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.certmgmt;

import io.ebean.annotation.EnumValue;

/*
 * various certificate configuration types
 */
public enum CertConfigType {
  @EnumValue("SelfSigned")
  SelfSigned,

  @EnumValue("CustomCertHostPath")
  CustomCertHostPath,

  @EnumValue("CustomServerCert")
  CustomServerCert,

  @EnumValue("HashicorpVault")
  HashicorpVault,

  @EnumValue("K8SCertManager")
  K8SCertManager
}
