// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.AzureRegionLocation;
import api.v2.models.AzureSasTokenCredentials;
import api.v2.models.AzureStorageSpec;
import api.v2.models.GcsServiceAccountKeyCredentials;
import api.v2.models.GcsStorageSpec;
import api.v2.models.OciS3CompatibleCredentials;
import api.v2.models.OciStorageSpec;
import api.v2.models.S3AccessKeyCredentials;
import api.v2.models.S3StorageSpec;
import api.v2.models.StorageSpec;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.util.HashMap;
import java.util.Map;

/**
 * Fills the secrets an edit of a storage config leaves out from the stored config. Secrets are
 * never returned, so a client editing a config can't send them back: a secret left out keeps the
 * stored one, when the stored config has the same credential type (an S3 config edited from IAM to
 * access keys needs its keys). An Azure region's SAS token is kept by region. The credentials a
 * spec can't express at all (S3 federation with keys, a GCS key kept alongside IAM) are kept by
 * StorageConfigHandler after the spec is mapped.
 */
final class StorageConfigSecrets {

  private StorageConfigSecrets() {}

  /** Fills {@code edited}'s missing secrets from {@code stored}, of the same storage type. */
  static void keepStored(StorageSpec edited, CustomerConfigData stored) {
    if (edited instanceof S3StorageSpec && stored instanceof CustomerConfigStorageS3Data) {
      keepStored((S3StorageSpec) edited, (CustomerConfigStorageS3Data) stored);
    } else if (edited instanceof GcsStorageSpec && stored instanceof CustomerConfigStorageGCSData) {
      keepStored((GcsStorageSpec) edited, (CustomerConfigStorageGCSData) stored);
    } else if (edited instanceof AzureStorageSpec
        && stored instanceof CustomerConfigStorageAzureData) {
      keepStored((AzureStorageSpec) edited, (CustomerConfigStorageAzureData) stored);
    } else if (edited instanceof OciStorageSpec && stored instanceof CustomerConfigStorageOCIData) {
      keepStored((OciStorageSpec) edited, (CustomerConfigStorageOCIData) stored);
    }
  }

  private static void keepStored(S3StorageSpec edited, CustomerConfigStorageS3Data stored) {
    if (!(edited.getCredentials() instanceof S3AccessKeyCredentials)
        || stored.isIAMInstanceProfile) {
      return;
    }
    S3AccessKeyCredentials keys = (S3AccessKeyCredentials) edited.getCredentials();
    if (keys.getAccessKeyId() == null) {
      keys.setAccessKeyId(stored.awsAccessKeyId);
    }
    if (keys.getSecretAccessKey() == null) {
      keys.setSecretAccessKey(stored.awsSecretAccessKey);
    }
  }

  private static void keepStored(GcsStorageSpec edited, CustomerConfigStorageGCSData stored) {
    if (!(edited.getCredentials() instanceof GcsServiceAccountKeyCredentials) || stored.useGcpIam) {
      return;
    }
    GcsServiceAccountKeyCredentials key = (GcsServiceAccountKeyCredentials) edited.getCredentials();
    if (key.getCredentialsJson() == null) {
      key.setCredentialsJson(stored.gcsCredentialsJson);
    }
  }

  private static void keepStored(AzureStorageSpec edited, CustomerConfigStorageAzureData stored) {
    if (!(edited.getCredentials() instanceof AzureSasTokenCredentials) || stored.useAzureIam) {
      return;
    }
    AzureSasTokenCredentials sas = (AzureSasTokenCredentials) edited.getCredentials();
    if (sas.getSasToken() == null) {
      sas.setSasToken(stored.azureSasToken);
    }
    if (edited.getRegionLocations() == null || stored.regionLocations == null) {
      return;
    }
    Map<String, String> storedTokens = new HashMap<>();
    for (CustomerConfigStorageAzureData.RegionLocations region : stored.regionLocations) {
      storedTokens.put(region.region, region.azureSasToken);
    }
    for (AzureRegionLocation region : edited.getRegionLocations()) {
      if (region.getSasToken() == null) {
        region.setSasToken(storedTokens.get(region.getRegion()));
      }
    }
  }

  private static void keepStored(OciStorageSpec edited, CustomerConfigStorageOCIData stored) {
    if (!(edited.getCredentials() instanceof OciS3CompatibleCredentials) || stored.useOciIam) {
      return;
    }
    OciS3CompatibleCredentials keys = (OciS3CompatibleCredentials) edited.getCredentials();
    if (keys.getAccessKeyId() == null) {
      keys.setAccessKeyId(stored.ociS3AccessKeyId);
    }
    if (keys.getSecretAccessKey() == null) {
      keys.setSecretAccessKey(stored.ociS3SecretAccessKey);
    }
  }
}
