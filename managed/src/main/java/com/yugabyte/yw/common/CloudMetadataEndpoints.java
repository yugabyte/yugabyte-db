// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

/**
 * Cloud instance metadata service endpoints, as ybops also uses them (GcpMetadata in
 * devops/opscli/ybops/cloud/gcp/utils.py, get_and_append_imdsv2_token_header in
 * devops/opscli/ybops/cloud/aws/cloud.py). They are link-local, so must be reached without a proxy.
 */
public final class CloudMetadataEndpoints {
  // AWS, GCP and Azure all serve instance metadata on this address.
  public static final String METADATA_IP = "169.254.169.254";
  public static final String GCP_METADATA_HOST = "metadata.google.internal";
  public static final String GCP_METADATA_FLAVOR_HEADER = "Metadata-Flavor";
  public static final String GCP_METADATA_FLAVOR = "Google";
  public static final String GCP_INSTANCE_ID_PATH = "/computeMetadata/v1/instance/id";
  public static final String AWS_IMDSV2_TOKEN_PATH = "/latest/api/token";
  public static final String AWS_IMDSV2_TOKEN_TTL_HEADER = "X-aws-ec2-metadata-token-ttl-seconds";
  public static final String AWS_INSTANCE_ID_PATH = "/latest/meta-data/instance-id";

  private CloudMetadataEndpoints() {}
}
