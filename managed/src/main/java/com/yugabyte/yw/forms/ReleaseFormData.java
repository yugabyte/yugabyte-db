// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata.GCSLocation;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata.HttpLocation;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata.S3Location;
import io.swagger.annotations.ApiModel;
import javax.validation.Valid;

@ApiModel(description = "Specification for release locations")
public class ReleaseFormData {

  public String version;

  @Valid public S3Location s3;

  @Valid public GCSLocation gcs;

  @Valid public HttpLocation http;
}
