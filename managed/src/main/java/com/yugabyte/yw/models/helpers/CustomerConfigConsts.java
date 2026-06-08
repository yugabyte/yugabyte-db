// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.Map;

public class CustomerConfigConsts {
  public static final String BACKUP_LOCATION_FIELDNAME = "BACKUP_LOCATION";

  public static final String REGION_LOCATION_FIELDNAME = "LOCATION";

  public static final String REGION_FIELDNAME = "REGION";

  public static final String REGION_LOCATIONS_FIELDNAME = "REGION_LOCATIONS";

  public static final String AWS_HOST_BASE_FIELDNAME = "AWS_HOST_BASE";

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";

  public static final String USE_GCP_IAM_FIELDNAME = "USE_GCP_IAM";

  public static final String USE_S3_IAM_FIELDNAME = "IAM_INSTANCE_PROFILE";

  public static final String USE_AZURE_IAM_FIELDNAME = "USE_AZURE_IAM";

  public static final String NAME_S3 = "S3";

  public static final String NAME_GCS = "GCS";

  public static final String NAME_NFS = "NFS";

  public static final String NAME_AZURE = "AZ";

  public static final Map<String, String> STORAGE_CONFIG_ARRAY_MERGE_FIELDS =
      Map.of(REGION_LOCATIONS_FIELDNAME, REGION_FIELDNAME);
}
