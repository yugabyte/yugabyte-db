// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.DefaultKubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientException;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.SharedInformerFactory;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.RestoreJob;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.SupportBundle;
import io.yugabyte.operator.v1alpha1.YBCertificate;
import java.lang.Thread.UncaughtExceptionHandler;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class KubernetesOperator {
  @Inject private ReleaseManager releaseManager;
  @Inject private GFlagsValidation gFlagsValidation;
  @Inject private CustomerConfigService ccs;
  @Inject private BackupHelper backupHelper;
  @Inject private DrConfigHelper drConfigHelper;
  @Inject protected ValidatingFormFactory formFactory;

  @Inject private Commissioner commissioner;

  @Inject private TaskExecutor taskExecutor;

  @Inject private SupportBundleUtil supportBundleUtil;

  @Inject private YBReconcilerFactory reconcilerFactory;

  @Inject private RuntimeConfGetter confGetter;
  @Inject private OperatorUtils operatorUtils;
  @Inject private CloudProviderHandler cloudProviderHandler;

  public MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> releasesClient;
  public MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>> backupClient;
  public MixedOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>>
      drConfigClient;
  public MixedOperation<RestoreJob, KubernetesResourceList<RestoreJob>, Resource<RestoreJob>>
      restoreJobClient;

  public MixedOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      scClient;
  public MixedOperation<
          SupportBundle, KubernetesResourceList<SupportBundle>, Resource<SupportBundle>>
      supportBundleClient;
  public MixedOperation<
          YBCertificate, KubernetesResourceList<YBCertificate>, Resource<YBCertificate>>
      ybCertificateClient;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOperator.class);

  public void init() {
    String namespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    LOG.info("Creating KubernetesOperator thread");
    Thread kubernetesOperatorThread =
        new Thread(
            () -> {
              try {
                long startTime = System.currentTimeMillis();
                LOG.info("Initiating the Kubernetes Operator objects");
                // Configuring client to watch the correct namesace here.
                Config config = new Config();
                if (StringUtils.isNotBlank(namespace)) {
                  config.setNamespace(namespace);
                } else {
                  config.setNamespace(null);
                }

                try (KubernetesClient client = new DefaultKubernetesClient(config)) {
                  LOG.info("Using namespace : {}", namespace);

                  this.releasesClient = client.resources(Release.class);
                  this.scClient = client.resources(StorageConfig.class);
                  this.backupClient = client.resources(Backup.class);
                  this.restoreJobClient = client.resources(RestoreJob.class);
                  this.drConfigClient = client.resources(DrConfig.class);

                  this.supportBundleClient = client.resources(SupportBundle.class);
                  this.ybCertificateClient = client.resources(YBCertificate.class);
                  SharedIndexInformer<Release> ybSoftwareReleaseIndexInformer;
                  SharedIndexInformer<StorageConfig> ybStorageConfigIndexInformer;
                  SharedIndexInformer<Backup> ybBackupIndexInformer;
                  SharedIndexInformer<DrConfig> ybDrConfigIndexInformer;
                  SharedIndexInformer<RestoreJob> ybRestoreJobIndexInformer;
                  SharedIndexInformer<YBCertificate> ybCertificateIndexInformer;

                  SharedIndexInformer<SupportBundle> ybSupportBundleIndexInformer;
                  long resyncPeriodInMillis = 10 * 60 * 1000L;
                  SharedInformerFactory informerFactory = client.informers();
                  if (StringUtils.isNotBlank(namespace)) {

                    // Listen to only one namespace.
                    ybSoftwareReleaseIndexInformer =
                        client
                            .resources(Release.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(Release r1) {}

                                  @Override
                                  public void onUpdate(Release r1, Release r2) {}

                                  @Override
                                  public void onDelete(Release r1, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);

                    ybStorageConfigIndexInformer =
                        client
                            .resources(StorageConfig.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(StorageConfig s) {}

                                  @Override
                                  public void onUpdate(StorageConfig s1, StorageConfig s2) {}

                                  @Override
                                  public void onDelete(
                                      StorageConfig s, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);

                    ybBackupIndexInformer =
                        client
                            .resources(Backup.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(Backup b) {}

                                  @Override
                                  public void onUpdate(Backup b1, Backup b2) {}

                                  @Override
                                  public void onDelete(Backup b, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);

                    ybDrConfigIndexInformer =
                        client
                            .resources(DrConfig.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(DrConfig d) {}

                                  @Override
                                  public void onUpdate(DrConfig d1, DrConfig d2) {}

                                  @Override
                                  public void onDelete(DrConfig b, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);

                    ybRestoreJobIndexInformer =
                        client
                            .resources(RestoreJob.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(RestoreJob r) {}

                                  @Override
                                  public void onUpdate(RestoreJob r1, RestoreJob r2) {}

                                  @Override
                                  public void onDelete(RestoreJob r, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);
                    ybSupportBundleIndexInformer =
                        client
                            .resources(SupportBundle.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(SupportBundle b1) {}

                                  @Override
                                  public void onUpdate(SupportBundle b1, SupportBundle b2) {}

                                  @Override
                                  public void onDelete(
                                      SupportBundle b1, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);
                    ybCertificateIndexInformer =
                        client
                            .resources(YBCertificate.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(YBCertificate cm) {}

                                  @Override
                                  public void onUpdate(YBCertificate cm1, YBCertificate cm2) {}

                                  @Override
                                  public void onDelete(
                                      YBCertificate cm, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);
                  } else {
                    // Listen to all namespaces, use the factory to build informer.
                    ybSoftwareReleaseIndexInformer =
                        informerFactory.sharedIndexInformerFor(Release.class, resyncPeriodInMillis);
                    ybStorageConfigIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            StorageConfig.class, resyncPeriodInMillis);
                    ybBackupIndexInformer =
                        informerFactory.sharedIndexInformerFor(Backup.class, resyncPeriodInMillis);
                    ybDrConfigIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            DrConfig.class, resyncPeriodInMillis);
                    ybRestoreJobIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            RestoreJob.class, resyncPeriodInMillis);
                    ybSupportBundleIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            SupportBundle.class, resyncPeriodInMillis);
                    ybCertificateIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            YBCertificate.class, resyncPeriodInMillis);
                  }
                  LOG.info("Finished setting up SharedIndexInformers");

                  YBUniverseReconciler ybUniverseController =
                      reconcilerFactory.getYBUniverseReconciler(client);

                  ScheduledBackupReconciler scheduledBackupReconciler =
                      reconcilerFactory.getScheduledBackupReconciler(client);

                  YBProviderReconciler ybProviderReconciler =
                      reconcilerFactory.getYBProviderReconciler(client);

                  PitrConfigReconciler pitrConfigReconciler =
                      reconcilerFactory.getPitrConfigReconciler(client);

                  DrConfigReconciler drConfigReconciler =
                      reconcilerFactory.getDrConfigReconciler(client);

                  ReleaseReconciler releaseReconciler =
                      new ReleaseReconciler(
                          ybSoftwareReleaseIndexInformer,
                          releasesClient,
                          releaseManager,
                          gFlagsValidation,
                          namespace,
                          confGetter,
                          operatorUtils);
                  SupportBundleReconciler supportBundleReconciler =
                      new SupportBundleReconciler(
                          ybSupportBundleIndexInformer,
                          supportBundleClient,
                          namespace,
                          commissioner,
                          taskExecutor,
                          supportBundleUtil,
                          operatorUtils);

                  StorageConfigReconciler scReconciler =
                      new StorageConfigReconciler(
                          ybStorageConfigIndexInformer, scClient, ccs, namespace, operatorUtils);

                  YBCertificateReconciler ybCertificateReconciler =
                      new YBCertificateReconciler(
                          ybCertificateIndexInformer,
                          ybCertificateClient,
                          namespace,
                          operatorUtils,
                          confGetter);

                  BackupReconciler backupReconciler =
                      new BackupReconciler(
                          ybBackupIndexInformer,
                          backupClient,
                          backupHelper,
                          formFactory,
                          namespace,
                          ybStorageConfigIndexInformer,
                          operatorUtils);

                  RestoreJobReconciler restoreJobReconciler =
                      new RestoreJobReconciler(
                          ybRestoreJobIndexInformer,
                          ybBackupIndexInformer,
                          restoreJobClient,
                          backupHelper,
                          formFactory,
                          namespace,
                          operatorUtils);

                  Future<Void> startedInformersFuture =
                      informerFactory.startAllRegisteredInformers();

                  startedInformersFuture.get();
                  releaseReconciler.run();
                  scReconciler.run();
                  ybCertificateReconciler.run();
                  backupReconciler.run();
                  restoreJobReconciler.run();
                  supportBundleReconciler.run();

                  // Async start reconcilers
                  Thread ybUniverseReconcilerThread = new Thread(() -> ybUniverseController.run());
                  Thread scheduledBackupReconcilerThread =
                      new Thread(() -> scheduledBackupReconciler.run());
                  Thread ybProviderReconcilerThread = new Thread(() -> ybProviderReconciler.run());
                  Thread pitrConfigReconcilerThread = new Thread(() -> pitrConfigReconciler.run());
                  Thread drConfigReconcilerThread = new Thread(() -> drConfigReconciler.run());
                  if (confGetter.getGlobalConf(
                      GlobalConfKeys.KubernetesOperatorCrashYbaOnOperatorFail)) {
                    Thread.UncaughtExceptionHandler exceptionHandler = getExceptionHandler();
                    ybUniverseReconcilerThread.setUncaughtExceptionHandler(exceptionHandler);
                    scheduledBackupReconcilerThread.setUncaughtExceptionHandler(exceptionHandler);
                    ybProviderReconcilerThread.setUncaughtExceptionHandler(exceptionHandler);
                    pitrConfigReconcilerThread.setUncaughtExceptionHandler(exceptionHandler);
                    drConfigReconcilerThread.setUncaughtExceptionHandler(exceptionHandler);
                  }
                  ybUniverseReconcilerThread.start();
                  scheduledBackupReconcilerThread.start();
                  ybProviderReconcilerThread.start();
                  pitrConfigReconcilerThread.start();
                  drConfigReconcilerThread.start();

                  ybUniverseReconcilerThread.join();
                  scheduledBackupReconcilerThread.join();
                  ybProviderReconcilerThread.join();
                  pitrConfigReconcilerThread.join();
                  drConfigReconcilerThread.join();

                  LOG.info(
                      "Finished running ybUniverseController, scheduledBackupReconciler,"
                          + " ybProviderReconcilerThread, pitrConfigReconcilerThread,"
                          + " drConfigReconcilerThread");
                } catch (KubernetesClientException | ExecutionException exception) {
                  LOG.error("Kubernetes Client Exception : ", exception);
                  throw new RuntimeException(
                      "Operator Initialization Failed to construct a kubernetes client");
                } catch (InterruptedException interruptedException) {
                  LOG.error("Interrupted: ", interruptedException);
                  throw new RuntimeException(
                      "Operator Initialization Failed, interupted by client");
                }
              } catch (Exception e) {
                LOG.error("Error", e);
                throw new RuntimeException("Operator Initialization Failed");
              }
            });

    // Add exception handler
    if (confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCrashYbaOnOperatorFail)) {
      kubernetesOperatorThread.setUncaughtExceptionHandler(getExceptionHandler());
    }
    kubernetesOperatorThread.start();
  }

  private Thread.UncaughtExceptionHandler getExceptionHandler() {
    return new UncaughtExceptionHandler() {
      @Override
      public void uncaughtException(Thread operatorThread, Throwable t) {
        LOG.error("Kubernetes operator thread failed", t);
        System.exit(1);
      }
    };
  }
}
