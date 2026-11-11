// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.StorageConfigMapper;
import api.v2.models.AzureCredentialType;
import api.v2.models.AzureManagedIdentityCredentials;
import api.v2.models.AzureRegionLocation;
import api.v2.models.AzureSasTokenCredentials;
import api.v2.models.AzureStorageSpec;
import api.v2.models.GcsCredentialType;
import api.v2.models.GcsCrossCloudFederationCredentials;
import api.v2.models.GcsIamCredentials;
import api.v2.models.GcsServiceAccountKeyCredentials;
import api.v2.models.GcsStorageSpec;
import api.v2.models.NfsRegionLocation;
import api.v2.models.NfsStorageSpec;
import api.v2.models.OciCredentialType;
import api.v2.models.OciIamCredentials;
import api.v2.models.OciS3CompatibleCredentials;
import api.v2.models.OciStorageSpec;
import api.v2.models.S3AccessKeyCredentials;
import api.v2.models.S3CredentialType;
import api.v2.models.S3CrossCloudFederationCredentials;
import api.v2.models.S3IamCredentialSource;
import api.v2.models.S3IamCredentials;
import api.v2.models.S3RegionLocation;
import api.v2.models.S3StorageSpec;
import api.v2.models.StorageConfigSpec;
import api.v2.models.StorageConfigState;
import api.v2.models.StorageSpec;
import api.v2.models.StorageType;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.IAMCredentialSource;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.util.List;
import java.util.UUID;
import org.junit.Test;

public class StorageConfigMapperTest {

  private static final StorageConfigMapper MAPPER = StorageConfigMapper.INSTANCE;
  private static final UUID CUSTOMER = UUID.randomUUID();

  private static CustomerConfig toV1(String name, StorageSpec storage) {
    return MAPPER.toV1CustomerConfig(CUSTOMER, new StorageConfigSpec().name(name).storage(storage));
  }

  /** v2 to v1 to v2: what a create stores and what a get then returns. */
  private static StorageSpec roundTrip(CustomerConfig v1) {
    v1.setConfigUUID(UUID.randomUUID());
    return MAPPER.toStorageConfig(v1).getSpec().getStorage();
  }

  @Test
  public void s3AccessKey() {
    S3StorageSpec spec =
        new S3StorageSpec()
            .storageType(StorageType.StorageTypeS3)
            .backupLocation("s3://bucket/path")
            .hostBase("s3.example.com")
            .signingRegion("us-east-2")
            .pathStyleAccess(true)
            .credentials(
                new S3AccessKeyCredentials()
                    .credentialType(S3CredentialType.S3CredentialAccessKey)
                    .accessKeyId("AKIA")
                    .secretAccessKey("secret"))
            .regionLocations(
                List.of(
                    new S3RegionLocation()
                        .region("us-west-2")
                        .location("s3://bucket-west/path")
                        .hostBase("s3.us-west-2.amazonaws.com")
                        .signingRegion("us-west-2")));

    CustomerConfig v1 = toV1(" s3-config ", spec);

    assertEquals(ConfigType.STORAGE, v1.getType());
    assertEquals("S3", v1.getName());
    assertEquals("s3-config", v1.getConfigName());
    assertEquals(CUSTOMER, v1.getCustomerUUID());
    JsonNode data = v1.getData();
    assertEquals("s3://bucket/path", data.get("BACKUP_LOCATION").asText());
    assertEquals("AKIA", data.get("AWS_ACCESS_KEY_ID").asText());
    assertEquals("secret", data.get("AWS_SECRET_ACCESS_KEY").asText());
    assertEquals("s3.example.com", data.get("AWS_HOST_BASE").asText());
    assertEquals("us-east-2", data.get("SIGNING_REGION").asText());
    assertTrue(data.get("PATH_STYLE_ACCESS").asBoolean());
    assertFalse(data.get("IAM_INSTANCE_PROFILE").asBoolean());
    // Defaults of the v1 data class are kept for what the client left out.
    assertTrue(data.get("GLOBAL_BUCKET_ACCESS").asBoolean());
    assertTrue(data.get("USE_CHUNKED_ENCODING").asBoolean());
    assertFalse("nulls aren't stored", data.has("PROXY_SETTINGS"));
    JsonNode region = data.get("REGION_LOCATIONS").get(0);
    assertEquals("us-west-2", region.get("REGION").asText());
    assertEquals("s3://bucket-west/path", region.get("LOCATION").asText());
    assertEquals("s3.us-west-2.amazonaws.com", region.get("AWS_HOST_BASE").asText());
    assertEquals("us-west-2", region.get("SIGNING_REGION").asText());
    assertTrue(region.get("GLOBAL_BUCKET_ACCESS").asBoolean());

    S3StorageSpec back = (S3StorageSpec) roundTrip(v1);
    S3AccessKeyCredentials keys = (S3AccessKeyCredentials) back.getCredentials();
    assertEquals("AKIA", keys.getAccessKeyId());
    assertNull("the secret isn't returned", keys.getSecretAccessKey());
    assertEquals(spec.getBackupLocation(), back.getBackupLocation());
    assertEquals(spec.getHostBase(), back.getHostBase());
    assertEquals(spec.getSigningRegion(), back.getSigningRegion());
    assertTrue(back.getPathStyleAccess());
    assertEquals(spec.getRegionLocations(), back.getRegionLocations());
  }

  @Test
  public void s3Iam() {
    CustomerConfig v1 =
        toV1(
            "s3-iam",
            new S3StorageSpec()
                .storageType(StorageType.StorageTypeS3)
                .backupLocation("s3://bucket")
                .credentials(
                    new S3IamCredentials()
                        .credentialType(S3CredentialType.S3CredentialIam)
                        .credentialSource(S3IamCredentialSource.S3IamSourceAssumeInstanceRole)
                        .sessionDurationSecs(7200)
                        .stsRegion("us-west-2")));

    CustomerConfigStorageS3Data data = (CustomerConfigStorageS3Data) v1.getDataObject();
    assertTrue(data.isIAMInstanceProfile);
    assertFalse(data.useCrossCloudFederation);
    assertNull(data.awsAccessKeyId);
    assertEquals(IAMCredentialSource.ASSUME_INSTANCE_ROLE, data.iamConfig.credentialSource);
    assertEquals(7200, data.iamConfig.duration);
    assertEquals("us-west-2", data.iamConfig.stsRegion);
    assertEquals("default", data.iamConfig.iamUserProfile);
    assertTrue(data.iamConfig.regionalSTS);

    S3IamCredentials back = (S3IamCredentials) ((S3StorageSpec) roundTrip(v1)).getCredentials();
    assertEquals(S3IamCredentialSource.S3IamSourceAssumeInstanceRole, back.getCredentialSource());
    assertEquals(Integer.valueOf(7200), back.getSessionDurationSecs());
    assertEquals("us-west-2", back.getStsRegion());
  }

  @Test
  public void s3CrossCloudFederation() {
    CustomerConfig v1 =
        toV1(
            "s3-federated",
            new S3StorageSpec()
                .storageType(StorageType.StorageTypeS3)
                .backupLocation("s3://bucket")
                .credentials(
                    new S3CrossCloudFederationCredentials()
                        .credentialType(S3CredentialType.S3CredentialCrossCloudFederation)));

    CustomerConfigStorageS3Data data = (CustomerConfigStorageS3Data) v1.getDataObject();
    // As the UI stores it.
    assertTrue(data.useCrossCloudFederation);
    assertTrue(data.isIAMInstanceProfile);
    assertTrue(
        ((S3StorageSpec) roundTrip(v1)).getCredentials()
            instanceof S3CrossCloudFederationCredentials);
  }

  @Test
  public void gcsCredentials() {
    CustomerConfig key =
        toV1(
            "gcs-key",
            gcs(
                new GcsServiceAccountKeyCredentials()
                    .credentialType(GcsCredentialType.GcsCredentialServiceAccountKey)
                    .credentialsJson("{\"type\": \"service_account\"}")));
    CustomerConfigStorageGCSData keyData = (CustomerConfigStorageGCSData) key.getDataObject();
    assertEquals("GCS", key.getName());
    assertFalse(keyData.useGcpIam);
    assertEquals("{\"type\": \"service_account\"}", keyData.gcsCredentialsJson);
    GcsServiceAccountKeyCredentials keyBack =
        (GcsServiceAccountKeyCredentials) ((GcsStorageSpec) roundTrip(key)).getCredentials();
    assertNull("the key isn't returned", keyBack.getCredentialsJson());

    CustomerConfig iam =
        toV1(
            "gcs-iam",
            gcs(new GcsIamCredentials().credentialType(GcsCredentialType.GcsCredentialIam)));
    CustomerConfigStorageGCSData iamData = (CustomerConfigStorageGCSData) iam.getDataObject();
    assertTrue(iamData.useGcpIam);
    assertFalse(iamData.useCrossCloudFederation);
    assertTrue(((GcsStorageSpec) roundTrip(iam)).getCredentials() instanceof GcsIamCredentials);

    CustomerConfig federated =
        toV1(
            "gcs-federated",
            gcs(
                new GcsCrossCloudFederationCredentials()
                    .credentialType(GcsCredentialType.GcsCredentialCrossCloudFederation)));
    CustomerConfigStorageGCSData federatedData =
        (CustomerConfigStorageGCSData) federated.getDataObject();
    assertTrue(federatedData.useGcpIam);
    assertTrue(federatedData.useCrossCloudFederation);
    assertTrue(
        ((GcsStorageSpec) roundTrip(federated)).getCredentials()
            instanceof GcsCrossCloudFederationCredentials);
  }

  private static GcsStorageSpec gcs(api.v2.models.GcsCredentials credentials) {
    return new GcsStorageSpec()
        .storageType(StorageType.StorageTypeGCS)
        .backupLocation("gs://bucket/path")
        .immutableStorage(true)
        .credentials(credentials);
  }

  @Test
  public void azureSasTokens() {
    CustomerConfig v1 =
        toV1(
            "azure-sas",
            new AzureStorageSpec()
                .storageType(StorageType.StorageTypeAzure)
                .backupLocation("https://account.blob.core.windows.net/container")
                .credentials(
                    new AzureSasTokenCredentials()
                        .credentialType(AzureCredentialType.AzureCredentialSasToken)
                        .sasToken("?sv=main"))
                .regionLocations(
                    List.of(
                        new AzureRegionLocation()
                            .region("westus2")
                            .location("https://west.blob.core.windows.net/container")
                            .sasToken("?sv=west"))));

    assertEquals("AZ", v1.getName());
    CustomerConfigStorageAzureData data = (CustomerConfigStorageAzureData) v1.getDataObject();
    assertFalse(data.useAzureIam);
    assertEquals("?sv=main", data.azureSasToken);
    assertEquals("?sv=west", data.regionLocations.get(0).azureSasToken);

    AzureStorageSpec back = (AzureStorageSpec) roundTrip(v1);
    assertNull(((AzureSasTokenCredentials) back.getCredentials()).getSasToken());
    assertEquals("westus2", back.getRegionLocations().get(0).getRegion());
    assertNull("regional tokens aren't returned", back.getRegionLocations().get(0).getSasToken());
  }

  @Test
  public void azureManagedIdentity() {
    CustomerConfig v1 =
        toV1(
            "azure-mi",
            new AzureStorageSpec()
                .storageType(StorageType.StorageTypeAzure)
                .backupLocation("https://account.blob.core.windows.net/container")
                .credentials(
                    new AzureManagedIdentityCredentials()
                        .credentialType(AzureCredentialType.AzureCredentialManagedIdentity)
                        .clientId("client-id")));

    CustomerConfigStorageAzureData data = (CustomerConfigStorageAzureData) v1.getDataObject();
    assertTrue(data.useAzureIam);
    assertEquals("client-id", data.azureClientId);
    assertEquals(
        "client-id",
        ((AzureManagedIdentityCredentials) ((AzureStorageSpec) roundTrip(v1)).getCredentials())
            .getClientId());
  }

  @Test
  public void nfsKeepsTheDefaultBucket() {
    CustomerConfig v1 =
        toV1(
            "nfs",
            new NfsStorageSpec()
                .storageType(StorageType.StorageTypeNFS)
                .backupLocation("/mnt/nfs")
                .regionLocations(
                    List.of(
                        new NfsRegionLocation()
                            .region("us-west1")
                            .location("/mnt/west")
                            .bucket("west_bucket"))));

    assertEquals("NFS", v1.getName());
    CustomerConfigStorageNFSData data = (CustomerConfigStorageNFSData) v1.getDataObject();
    assertEquals("yugabyte_backup", data.nfsBucket);
    assertEquals("west_bucket", data.regionLocations.get(0).nfsBucket);

    NfsStorageSpec back = (NfsStorageSpec) roundTrip(v1);
    assertEquals("/mnt/nfs", back.getBackupLocation());
    assertEquals("yugabyte_backup", back.getBucket());
    assertEquals("/mnt/west", back.getRegionLocations().get(0).getLocation());
  }

  @Test
  public void ociCredentials() {
    CustomerConfig keys =
        toV1(
            "oci-keys",
            oci(
                new OciS3CompatibleCredentials()
                    .credentialType(OciCredentialType.OciCredentialS3Compatible)
                    .accessKeyId("key")
                    .secretAccessKey("secret")
                    .hostBase("ns.compat.objectstorage.us-sanjose-1.oraclecloud.com")));
    assertEquals("OCI", keys.getName());
    CustomerConfigStorageOCIData keysData = (CustomerConfigStorageOCIData) keys.getDataObject();
    assertFalse(keysData.useOciIam);
    assertEquals("us-sanjose-1", keysData.ociRegion);
    assertEquals("key", keysData.ociS3AccessKeyId);
    assertEquals("secret", keysData.ociS3SecretAccessKey);
    OciS3CompatibleCredentials keysBack =
        (OciS3CompatibleCredentials) ((OciStorageSpec) roundTrip(keys)).getCredentials();
    assertEquals("key", keysBack.getAccessKeyId());
    assertEquals("ns.compat.objectstorage.us-sanjose-1.oraclecloud.com", keysBack.getHostBase());
    assertNull(keysBack.getSecretAccessKey());

    CustomerConfig iam =
        toV1(
            "oci-iam",
            oci(new OciIamCredentials().credentialType(OciCredentialType.OciCredentialIam)));
    CustomerConfigStorageOCIData iamData = (CustomerConfigStorageOCIData) iam.getDataObject();
    assertTrue(iamData.useOciIam);
    assertEquals("ns", iamData.ociNamespace);
    assertTrue(((OciStorageSpec) roundTrip(iam)).getCredentials() instanceof OciIamCredentials);
  }

  private static OciStorageSpec oci(api.v2.models.OciCredentials credentials) {
    return new OciStorageSpec()
        .storageType(StorageType.StorageTypeOCI)
        .backupLocation("s3://bucket")
        .region("us-sanjose-1")
        .namespace("ns")
        .credentials(credentials);
  }

  @Test
  public void info() {
    CustomerConfig v1 =
        toV1(
            "nfs",
            new NfsStorageSpec().storageType(StorageType.StorageTypeNFS).backupLocation("/mnt"));
    v1.setConfigUUID(UUID.randomUUID());
    v1.setState(ConfigState.QueuedForDeletion);

    api.v2.models.StorageConfigInfo info = MAPPER.toStorageConfigInfo(v1);
    assertEquals(v1.getConfigUUID(), info.getUuid());
    assertEquals(StorageConfigState.StorageConfigQueuedForDeletion, info.getState());
    assertFalse(info.getKubernetesOperatorControlled());
  }
}
