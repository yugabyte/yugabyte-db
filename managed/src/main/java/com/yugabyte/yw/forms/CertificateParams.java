// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.Date;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for
 * the custom certificate Data.
 */
public class CertificateParams {
  @Constraints.Required()
  public String label;

  @Constraints.Required()
  public long certStart;

  @Constraints.Required()
  public long certExpiry;

  @Constraints.Required()
  public String certContent;

  @Constraints.Required()
  public String keyContent;

}
