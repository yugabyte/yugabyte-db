// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_AZURE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_GCS;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_NFS;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_OCI;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_S3;

import api.v2.models.AzureCredentialType;
import api.v2.models.AzureCredentials;
import api.v2.models.AzureManagedIdentityCredentials;
import api.v2.models.AzureRegionLocation;
import api.v2.models.AzureSasTokenCredentials;
import api.v2.models.AzureStorageSpec;
import api.v2.models.GcsCredentialType;
import api.v2.models.GcsCredentials;
import api.v2.models.GcsCrossCloudFederationCredentials;
import api.v2.models.GcsIamCredentials;
import api.v2.models.GcsRegionLocation;
import api.v2.models.GcsServiceAccountKeyCredentials;
import api.v2.models.GcsStorageSpec;
import api.v2.models.NfsRegionLocation;
import api.v2.models.NfsStorageSpec;
import api.v2.models.OciCredentialType;
import api.v2.models.OciCredentials;
import api.v2.models.OciIamCredentials;
import api.v2.models.OciRegionLocation;
import api.v2.models.OciS3CompatibleCredentials;
import api.v2.models.OciStorageSpec;
import api.v2.models.S3AccessKeyCredentials;
import api.v2.models.S3CredentialType;
import api.v2.models.S3Credentials;
import api.v2.models.S3CrossCloudFederationCredentials;
import api.v2.models.S3IamCredentialSource;
import api.v2.models.S3IamCredentials;
import api.v2.models.S3RegionLocation;
import api.v2.models.S3StorageSpec;
import api.v2.models.StorageConfig;
import api.v2.models.StorageConfigInfo;
import api.v2.models.StorageConfigSpec;
import api.v2.models.StorageConfigState;
import api.v2.models.StorageSpec;
import api.v2.models.StorageType;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.IAMTemporaryCredentialsProvider.IAMCredentialSource;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.util.List;
import java.util.UUID;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.NullValueCheckStrategy;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

/**
 * Converts between the v2 storage config resource and the v1 {@link CustomerConfig} of type
 * STORAGE, whose subtype is in {@code name} and whose settings are an untyped JSON {@code data}.
 *
 * <p>The v2 spec is a oneOf by storage type, with a oneOf of credentials per storage type, while v1
 * data marks the kind of credentials with flags (IAM_INSTANCE_PROFILE, USE_GCP_IAM,
 * USE_CROSS_CLOUD_FEDERATION, USE_AZURE_IAM, USE_OCI_IAM). The default methods pick the variant;
 * MapStruct generates the field mappings of the region locations. Secrets and access key IDs go
 * from v2 to v1 only: the v1 to v2 direction never sets them, so responses don't carry them.
 *
 * <p>Null checks keep the defaults of the v1 data classes (such as NFS_BUCKET and
 * GLOBAL_BUCKET_ACCESS) for properties the client omits.
 */
@Mapper(config = CentralConfig.class, nullValueCheckStrategy = NullValueCheckStrategy.ALWAYS)
public interface StorageConfigMapper {

  StorageConfigMapper INSTANCE = Mappers.getMapper(StorageConfigMapper.class);

  // Leaves out nulls, so that the stored data holds only what was set, as with v1 clients.
  ObjectMapper DATA_MAPPER =
      Json.mapper().copy().setSerializationInclusion(JsonInclude.Include.NON_NULL);

  // ---- v1 to v2 ----

  default StorageConfig toStorageConfig(CustomerConfig config) {
    return new StorageConfig().spec(toStorageConfigSpec(config)).info(toStorageConfigInfo(config));
  }

  default StorageConfigSpec toStorageConfigSpec(CustomerConfig config) {
    return new StorageConfigSpec()
        .name(config.getConfigName())
        .storage(toStorageSpec(config.getDataObject()));
  }

  default StorageConfigInfo toStorageConfigInfo(CustomerConfig config) {
    return new StorageConfigInfo()
        .uuid(config.getConfigUUID())
        .state(toStorageConfigState(config.getState()))
        .isKubernetesOperatorControlled(config.isKubernetesOperatorControlled());
  }

  default StorageConfigState toStorageConfigState(ConfigState state) {
    if (state == ConfigState.QueuedForDeletion) {
      return StorageConfigState.StorageConfigQueuedForDeletion;
    }
    if (state == ConfigState.Validating) {
      return StorageConfigState.StorageConfigValidating;
    }
    return StorageConfigState.StorageConfigActive;
  }

  default StorageSpec toStorageSpec(CustomerConfigData data) {
    if (data instanceof CustomerConfigStorageS3Data) {
      CustomerConfigStorageS3Data s3 = (CustomerConfigStorageS3Data) data;
      return toS3StorageSpec(s3);
    } else if (data instanceof CustomerConfigStorageGCSData) {
      CustomerConfigStorageGCSData gcs = (CustomerConfigStorageGCSData) data;
      return toGcsStorageSpec(gcs);
    } else if (data instanceof CustomerConfigStorageAzureData) {
      CustomerConfigStorageAzureData azure = (CustomerConfigStorageAzureData) data;
      return toAzureStorageSpec(azure);
    } else if (data instanceof CustomerConfigStorageNFSData) {
      CustomerConfigStorageNFSData nfs = (CustomerConfigStorageNFSData) data;
      return toNfsStorageSpec(nfs);
    } else if (data instanceof CustomerConfigStorageOCIData) {
      CustomerConfigStorageOCIData oci = (CustomerConfigStorageOCIData) data;
      return toOciStorageSpec(oci);
    }
    throw new IllegalArgumentException("Not storage config data: " + data.getClass());
  }

  default S3StorageSpec toS3StorageSpec(CustomerConfigStorageS3Data data) {
    S3Credentials credentials;
    if (data.useCrossCloudFederation) {
      credentials =
          new S3CrossCloudFederationCredentials()
              .credentialType(S3CredentialType.S3CredentialCrossCloudFederation);
    } else if (data.isIAMInstanceProfile) {
      CustomerConfigStorageS3Data.IAMConfiguration iam =
          data.iamConfig != null
              ? data.iamConfig
              : new CustomerConfigStorageS3Data.IAMConfiguration();
      credentials =
          new S3IamCredentials()
              .credentialType(S3CredentialType.S3CredentialIam)
              .credentialSource(
                  iam.credentialSource == null
                      ? S3IamCredentialSource.S3IamSourceDefault
                      : S3IamCredentialSource.fromValue(iam.credentialSource.name()))
              .iamUserProfile(iam.iamUserProfile)
              .sessionDurationSecs(iam.duration)
              .regionalSts(iam.regionalSTS)
              .stsRegion(iam.stsRegion);
    } else {
      credentials =
          new S3AccessKeyCredentials().credentialType(S3CredentialType.S3CredentialAccessKey);
    }
    return new S3StorageSpec()
        .storageType(StorageType.StorageTypeS3)
        .backupLocation(data.backupLocation)
        .immutableStorage(data.immutableStorage)
        .hostBase(data.awsHostBase)
        .signingRegion(data.fallbackRegion)
        .pathStyleAccess(data.isPathStyleAccess)
        .useChunkedEncoding(data.useChunkedEncoding)
        .globalBucketAccess(data.globalBucketAccess)
        .credentials(credentials)
        .regionLocations(toS3RegionLocations(data.regionLocations));
  }

  default GcsStorageSpec toGcsStorageSpec(CustomerConfigStorageGCSData data) {
    GcsCredentials credentials;
    if (!data.useGcpIam) {
      credentials =
          new GcsServiceAccountKeyCredentials()
              .credentialType(GcsCredentialType.GcsCredentialServiceAccountKey);
    } else if (data.useCrossCloudFederation) {
      credentials =
          new GcsCrossCloudFederationCredentials()
              .credentialType(GcsCredentialType.GcsCredentialCrossCloudFederation);
    } else {
      credentials = new GcsIamCredentials().credentialType(GcsCredentialType.GcsCredentialIam);
    }
    return new GcsStorageSpec()
        .storageType(StorageType.StorageTypeGCS)
        .backupLocation(data.backupLocation)
        .immutableStorage(data.immutableStorage)
        .credentials(credentials)
        .regionLocations(toGcsRegionLocations(data.regionLocations));
  }

  default AzureStorageSpec toAzureStorageSpec(CustomerConfigStorageAzureData data) {
    AzureCredentials credentials =
        data.useAzureIam
            ? new AzureManagedIdentityCredentials()
                .credentialType(AzureCredentialType.AzureCredentialManagedIdentity)
                .clientId(data.azureClientId)
            : new AzureSasTokenCredentials()
                .credentialType(AzureCredentialType.AzureCredentialSasToken);
    return new AzureStorageSpec()
        .storageType(StorageType.StorageTypeAzure)
        .backupLocation(data.backupLocation)
        .immutableStorage(data.immutableStorage)
        .credentials(credentials)
        .regionLocations(toAzureRegionLocations(data.regionLocations));
  }

  default NfsStorageSpec toNfsStorageSpec(CustomerConfigStorageNFSData data) {
    return new NfsStorageSpec()
        .storageType(StorageType.StorageTypeNFS)
        .backupLocation(data.backupLocation)
        .bucket(data.nfsBucket)
        .volumes(data.nfsVolumes)
        .regionLocations(toNfsRegionLocations(data.regionLocations));
  }

  default OciStorageSpec toOciStorageSpec(CustomerConfigStorageOCIData data) {
    OciCredentials credentials =
        data.useOciIam
            ? new OciIamCredentials().credentialType(OciCredentialType.OciCredentialIam)
            : new OciS3CompatibleCredentials()
                .credentialType(OciCredentialType.OciCredentialS3Compatible)
                .hostBase(data.ociS3HostBase);
    return new OciStorageSpec()
        .storageType(StorageType.StorageTypeOCI)
        .backupLocation(data.backupLocation)
        .immutableStorage(data.immutableStorage)
        .region(data.ociRegion)
        .namespace(data.ociNamespace)
        .credentials(credentials)
        .regionLocations(toOciRegionLocations(data.regionLocations));
  }

  @Mapping(target = "hostBase", source = "awsHostBase")
  @Mapping(target = "signingRegion", source = "fallbackRegion")
  S3RegionLocation toS3RegionLocation(CustomerConfigStorageS3Data.RegionLocations source);

  List<S3RegionLocation> toS3RegionLocations(
      List<CustomerConfigStorageS3Data.RegionLocations> source);

  GcsRegionLocation toGcsRegionLocation(CustomerConfigStorageGCSData.RegionLocations source);

  List<GcsRegionLocation> toGcsRegionLocations(
      List<CustomerConfigStorageGCSData.RegionLocations> source);

  // The regional SAS token is a secret: never returned.
  @Mapping(target = "sasToken", ignore = true)
  AzureRegionLocation toAzureRegionLocation(CustomerConfigStorageAzureData.RegionLocations source);

  List<AzureRegionLocation> toAzureRegionLocations(
      List<CustomerConfigStorageAzureData.RegionLocations> source);

  @Mapping(target = "bucket", source = "nfsBucket")
  @Mapping(target = "volumes", source = "nfsVolumes")
  NfsRegionLocation toNfsRegionLocation(CustomerConfigStorageNFSData.RegionLocations source);

  List<NfsRegionLocation> toNfsRegionLocations(
      List<CustomerConfigStorageNFSData.RegionLocations> source);

  @Mapping(target = "hostBase", source = "ociS3HostBase")
  OciRegionLocation toOciRegionLocation(CustomerConfigStorageOCIData.RegionLocations source);

  List<OciRegionLocation> toOciRegionLocations(
      List<CustomerConfigStorageOCIData.RegionLocations> source);

  // ---- v2 to v1 ----

  /**
   * Builds the v1 storage config of the customer from a v2 spec, without a UUID and not saved. The
   * handler has checked that the spec carries the secrets its credentials need.
   */
  default CustomerConfig toV1CustomerConfig(UUID customerUUID, StorageConfigSpec spec) {
    StorageSpec storage = spec.getStorage();
    CustomerConfigData data;
    String subtype;
    if (storage instanceof S3StorageSpec) {
      S3StorageSpec s3 = (S3StorageSpec) storage;
      data = toV1Data(s3);
      subtype = NAME_S3;
    } else if (storage instanceof GcsStorageSpec) {
      GcsStorageSpec gcs = (GcsStorageSpec) storage;
      data = toV1Data(gcs);
      subtype = NAME_GCS;
    } else if (storage instanceof AzureStorageSpec) {
      AzureStorageSpec azure = (AzureStorageSpec) storage;
      data = toV1Data(azure);
      subtype = NAME_AZURE;
    } else if (storage instanceof NfsStorageSpec) {
      NfsStorageSpec nfs = (NfsStorageSpec) storage;
      data = toV1Data(nfs);
      subtype = NAME_NFS;
    } else if (storage instanceof OciStorageSpec) {
      OciStorageSpec oci = (OciStorageSpec) storage;
      data = toV1Data(oci);
      subtype = NAME_OCI;
    } else {
      throw new IllegalArgumentException("Unknown storage spec: " + storage.getClass());
    }
    return new CustomerConfig()
        .setCustomerUUID(customerUUID)
        .setType(ConfigType.STORAGE)
        .setName(subtype)
        .setConfigName(spec.getName())
        .setData((ObjectNode) DATA_MAPPER.valueToTree(data));
  }

  default CustomerConfigStorageS3Data toV1Data(S3StorageSpec spec) {
    CustomerConfigStorageS3Data data = new CustomerConfigStorageS3Data();
    data.backupLocation = spec.getBackupLocation();
    if (spec.getImmutableStorage() != null) {
      data.immutableStorage = spec.getImmutableStorage();
    }
    data.awsHostBase = spec.getHostBase();
    data.fallbackRegion = spec.getSigningRegion();
    if (spec.getPathStyleAccess() != null) {
      data.isPathStyleAccess = spec.getPathStyleAccess();
    }
    if (spec.getUseChunkedEncoding() != null) {
      data.useChunkedEncoding = spec.getUseChunkedEncoding();
    }
    if (spec.getGlobalBucketAccess() != null) {
      data.globalBucketAccess = spec.getGlobalBucketAccess();
    }
    S3Credentials credentials = spec.getCredentials();
    if (credentials instanceof S3AccessKeyCredentials) {
      S3AccessKeyCredentials keys = (S3AccessKeyCredentials) credentials;
      data.awsAccessKeyId = keys.getAccessKeyId();
      data.awsSecretAccessKey = keys.getSecretAccessKey();
    } else if (credentials instanceof S3IamCredentials) {
      S3IamCredentials iam = (S3IamCredentials) credentials;
      data.isIAMInstanceProfile = true;
      if (iam.getCredentialSource() != null) {
        data.iamConfig.credentialSource =
            IAMCredentialSource.valueOf(iam.getCredentialSource().toString());
      }
      if (iam.getIamUserProfile() != null) {
        data.iamConfig.iamUserProfile = iam.getIamUserProfile();
      }
      if (iam.getSessionDurationSecs() != null) {
        data.iamConfig.duration = iam.getSessionDurationSecs();
      }
      if (iam.getRegionalSts() != null) {
        data.iamConfig.regionalSTS = iam.getRegionalSts();
      }
      data.iamConfig.stsRegion = iam.getStsRegion();
    } else if (credentials instanceof S3CrossCloudFederationCredentials) {
      // As the UI creates it: the nodes on GCP federate, and YBA falls back to its own IAM
      // identity wherever the role and audience of a universe's provider aren't known.
      data.isIAMInstanceProfile = true;
      data.useCrossCloudFederation = true;
    }
    data.regionLocations = toV1S3RegionLocations(spec.getRegionLocations());
    return data;
  }

  default CustomerConfigStorageGCSData toV1Data(GcsStorageSpec spec) {
    CustomerConfigStorageGCSData data = new CustomerConfigStorageGCSData();
    data.backupLocation = spec.getBackupLocation();
    if (spec.getImmutableStorage() != null) {
      data.immutableStorage = spec.getImmutableStorage();
    }
    GcsCredentials credentials = spec.getCredentials();
    if (credentials instanceof GcsServiceAccountKeyCredentials) {
      GcsServiceAccountKeyCredentials key = (GcsServiceAccountKeyCredentials) credentials;
      data.gcsCredentialsJson = key.getCredentialsJson();
    } else if (credentials instanceof GcsIamCredentials) {
      data.useGcpIam = true;
    } else if (credentials instanceof GcsCrossCloudFederationCredentials) {
      data.useGcpIam = true;
      data.useCrossCloudFederation = true;
    }
    data.regionLocations = toV1GcsRegionLocations(spec.getRegionLocations());
    return data;
  }

  default CustomerConfigStorageAzureData toV1Data(AzureStorageSpec spec) {
    CustomerConfigStorageAzureData data = new CustomerConfigStorageAzureData();
    data.backupLocation = spec.getBackupLocation();
    if (spec.getImmutableStorage() != null) {
      data.immutableStorage = spec.getImmutableStorage();
    }
    AzureCredentials credentials = spec.getCredentials();
    if (credentials instanceof AzureSasTokenCredentials) {
      AzureSasTokenCredentials sas = (AzureSasTokenCredentials) credentials;
      data.azureSasToken = sas.getSasToken();
    } else if (credentials instanceof AzureManagedIdentityCredentials) {
      AzureManagedIdentityCredentials identity = (AzureManagedIdentityCredentials) credentials;
      data.useAzureIam = true;
      data.azureClientId = identity.getClientId();
    }
    data.regionLocations = toV1AzureRegionLocations(spec.getRegionLocations());
    return data;
  }

  default CustomerConfigStorageNFSData toV1Data(NfsStorageSpec spec) {
    CustomerConfigStorageNFSData data = new CustomerConfigStorageNFSData();
    data.backupLocation = spec.getBackupLocation();
    if (spec.getBucket() != null) {
      data.nfsBucket = spec.getBucket();
    }
    data.nfsVolumes = spec.getVolumes();
    data.regionLocations = toV1NfsRegionLocations(spec.getRegionLocations());
    return data;
  }

  default CustomerConfigStorageOCIData toV1Data(OciStorageSpec spec) {
    CustomerConfigStorageOCIData data = new CustomerConfigStorageOCIData();
    data.backupLocation = spec.getBackupLocation();
    if (spec.getImmutableStorage() != null) {
      data.immutableStorage = spec.getImmutableStorage();
    }
    data.ociRegion = spec.getRegion();
    data.ociNamespace = spec.getNamespace();
    OciCredentials credentials = spec.getCredentials();
    if (credentials instanceof OciIamCredentials) {
      data.useOciIam = true;
    } else if (credentials instanceof OciS3CompatibleCredentials) {
      OciS3CompatibleCredentials keys = (OciS3CompatibleCredentials) credentials;
      data.ociS3AccessKeyId = keys.getAccessKeyId();
      data.ociS3SecretAccessKey = keys.getSecretAccessKey();
      data.ociS3HostBase = keys.getHostBase();
    }
    data.regionLocations = toV1OciRegionLocations(spec.getRegionLocations());
    return data;
  }

  @Mapping(target = "awsHostBase", source = "hostBase")
  @Mapping(target = "fallbackRegion", source = "signingRegion")
  CustomerConfigStorageS3Data.RegionLocations toV1S3RegionLocation(S3RegionLocation source);

  List<CustomerConfigStorageS3Data.RegionLocations> toV1S3RegionLocations(
      List<S3RegionLocation> source);

  CustomerConfigStorageGCSData.RegionLocations toV1GcsRegionLocation(GcsRegionLocation source);

  List<CustomerConfigStorageGCSData.RegionLocations> toV1GcsRegionLocations(
      List<GcsRegionLocation> source);

  @Mapping(target = "azureSasToken", source = "sasToken")
  CustomerConfigStorageAzureData.RegionLocations toV1AzureRegionLocation(
      AzureRegionLocation source);

  List<CustomerConfigStorageAzureData.RegionLocations> toV1AzureRegionLocations(
      List<AzureRegionLocation> source);

  @Mapping(target = "nfsBucket", source = "bucket")
  @Mapping(target = "nfsVolumes", source = "volumes")
  CustomerConfigStorageNFSData.RegionLocations toV1NfsRegionLocation(NfsRegionLocation source);

  List<CustomerConfigStorageNFSData.RegionLocations> toV1NfsRegionLocations(
      List<NfsRegionLocation> source);

  @Mapping(target = "ociS3HostBase", source = "hostBase")
  CustomerConfigStorageOCIData.RegionLocations toV1OciRegionLocation(OciRegionLocation source);

  List<CustomerConfigStorageOCIData.RegionLocations> toV1OciRegionLocations(
      List<OciRegionLocation> source);
}
