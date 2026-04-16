package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseArtifact.Platform;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.inject.guice.GuiceApplicationBuilder;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class OperatorImportUniverseTest extends CommissionerBaseTest {

  private OperatorUtils operatorUtils;

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    operatorUtils = mock(OperatorUtils.class);
    return super.configureApplication(builder)
        .overrides(bind(OperatorUtils.class).toInstance(operatorUtils))
        .overrides(bind(ReleasesUtils.class).toInstance(mockReleasesUtils));
  }

  private TaskInfo submitTask(OperatorImportUniverse.Params taskParams) {
    UUID taskUUID = commissioner.submit(TaskType.OperatorImportUniverse, taskParams);
    try {
      return waitForTask(taskUUID);
    } catch (Exception e) {
      throw new RuntimeException("Failed to submit task", e);
    }
  }

  @Test
  public void testImportUniverse() throws IOException {
    when(mockReleasesUtils.versionUniversesMap()).thenReturn(new HashMap<String, List<Universe>>());
    String version = "2025.2.0.0-b123";
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.kubernetesProvider(customer);
    File f = new File("/tmp/kubeconfig");
    f.createNewFile();
    FileWriter fw = new FileWriter(f);
    fw.write("test");
    fw.close();
    ProviderDetails pd = provider.getDetails();
    CloudInfo cloudInfo = new CloudInfo();
    KubernetesInfo kubernetesInfo = new KubernetesInfo();
    kubernetesInfo.setKubeConfig("/tmp/kubeconfig");
    cloudInfo.setKubernetes(kubernetesInfo);
    pd.setCloudInfo(cloudInfo);
    provider.setDetails(pd);
    provider.save();
    Region region = Region.create(provider, "region1", "region1", "yb-image-1");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az1", "az1", "subnet1");
    AvailabilityZoneDetails azdetails = new AvailabilityZoneDetails();
    KubernetesRegionInfo azki = new KubernetesRegionInfo();
    azki.setKubeConfigContent("myContent");
    AvailabilityZoneDetails.AZCloudInfo azcloudInfo = new AvailabilityZoneDetails.AZCloudInfo();
    azcloudInfo.setKubernetes(azki);
    azdetails.setCloudInfo(azcloudInfo);
    az.setDetails(azdetails);
    az.save();
    region.setZones(Arrays.asList(az));
    region.save();
    provider.setRegions(Arrays.asList(region));
    provider.save();
    Universe universe =
        ModelFactory.createUniverse("import-uni", customer.getId(), CloudType.kubernetes);

    // Create releases
    Release release = Release.create(version, "stable(lts)");
    ReleaseArtifact ra1 =
        ReleaseArtifact.create(
            "sha256", Platform.KUBERNETES, null, "https://example.com/yugabyte-kubernetes.tar.gz");
    ReleaseArtifact ra2 =
        ReleaseArtifact.create(
            "sha256-2",
            Platform.LINUX,
            Architecture.x86_64,
            "https://example.com/yugabyte-x86_64.tar.gz");
    release.addArtifact(ra1);
    release.addArtifact(ra2);
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.getPrimaryCluster().userIntent.ybSoftwareVersion = version;
          u.setUniverseDetails(details);
        });

    // Storage Configs
    CustomerConfig sc1;
    CustomerConfig sc2;
    CustomerConfig sc3;
    CustomerConfig sc4;
    ObjectMapper objectMapper = new ObjectMapper();
    try {
      sc1 =
          CustomerConfig.createStorageConfig(
              customer.getUuid(),
              "S3",
              "storage_config1",
              objectMapper.readTree("{\"AWS_SECRET_ACCESS_KEY\":\"your_aws_secret_key\"}"));
      sc1.generateUUID();
      sc1.save();
      sc2 =
          CustomerConfig.createStorageConfig(
              customer.getUuid(),
              "GCS",
              "storage_config2",
              objectMapper.readTree("{\"GCS_CREDENTIALS_JSON\":\"your_gcs_credentials_json\"}"));
      sc2.generateUUID();
      sc2.save();
      sc3 =
          CustomerConfig.createStorageConfig(
              customer.getUuid(),
              "AZ",
              "storage_config3",
              objectMapper.readTree(
                  "{\"AZURE_STORAGE_SAS_TOKEN\":\"your_azure_storage_sas_token\"}"));
      sc3.generateUUID();
      sc3.save();
      sc4 =
          CustomerConfig.createStorageConfig(
              customer.getUuid(),
              "NFS",
              "storage_config4",
              objectMapper.readTree(
                  "{\"NFS_SERVER\":\"your_nfs_server\",\"NFS_PATH\":\"/path/to/nfs\"}"));
      sc4.generateUUID();
      sc4.save();
    } catch (Exception e) {
      throw new RuntimeException("Failed to create storage configs", e);
    }

    BackupTableParams bkpParams = new BackupTableParams();
    bkpParams.setUniverseUUID(universe.getUniverseUUID());
    bkpParams.storageLocation = "/path/to/backup";

    bkpParams.storageConfigUUID = sc1.getConfigUUID();
    Backup bkp1 =
        Backup.create(
            customer.getUuid(),
            bkpParams,
            Backup.BackupCategory.YB_CONTROLLER,
            Backup.BackupVersion.V2);
    bkpParams.storageConfigUUID = sc2.getConfigUUID();
    Backup bkp2 =
        Backup.create(
            customer.getUuid(),
            bkpParams,
            Backup.BackupCategory.YB_CONTROLLER,
            Backup.BackupVersion.V2);

    bkpParams.storageConfigUUID = sc3.getConfigUUID();
    Schedule sch1 =
        Schedule.create(
            customer.getUuid(),
            bkpParams,
            TaskType.CreateBackup,
            60L, // Frequency
            "" /* cron */);

    bkpParams.storageConfigUUID = sc4.getConfigUUID();
    Schedule sch2 =
        Schedule.create(
            customer.getUuid(),
            bkpParams,
            TaskType.CreateBackup,
            60L, // Frequency
            "" /* cron */);

    OperatorImportUniverse.Params taskParams = new OperatorImportUniverse.Params();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.namespace = "default";
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(19, subTasks.size());
  }
}
