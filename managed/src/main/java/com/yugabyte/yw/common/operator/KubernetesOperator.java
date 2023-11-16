// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
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
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.SupportBundle;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class KubernetesOperator {
  @Inject private UniverseCRUDHandler universeCRUDHandler;

  @Inject private UpgradeUniverseHandler upgradeUniverseHandler;

  @Inject private CloudProviderHandler cloudProviderHandler;

  @Inject private ReleaseManager releaseManager;
  @Inject private GFlagsValidation gFlagsValidation;
  @Inject private CustomerConfigService ccs;
  @Inject private BackupHelper backupHelper;
  @Inject protected ValidatingFormFactory formFactory;

  @Inject private Commissioner commissioner;

  @Inject private TaskExecutor taskExecutor;

  @Inject private SupportBundleUtil supportBundleUtil;

  @Inject KubernetesOperatorStatusUpdater kubernetesStatusUpdater;

  public MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;

  public MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> releasesClient;
  public MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>> backupClient;

  public MixedOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      scClient;
  public MixedOperation<
          SupportBundle, KubernetesResourceList<SupportBundle>, Resource<SupportBundle>>
      supportBundleClient;

  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOperator.class);

  public void init(String namespace) {
    LOG.info("Creating KubernetesOperator thread");
    Thread kubernetesOperatorThread =
        new Thread(
            () -> {
              try {
                long startTime = System.currentTimeMillis();
                LOG.info("Initiating the Kubernetes Operator objects");
                // Configuring client to watch the correct namesace here.
                Config config = new Config();
                if (namespace.trim().isEmpty()) {
                  config.setNamespace(namespace);
                } else {
                  config.setNamespace(null);
                }

                try (KubernetesClient client = new DefaultKubernetesClient(config)) {
                  LOG.info("Using namespace : {}", namespace);

                  this.ybUniverseClient = client.resources(YBUniverse.class);
                  this.releasesClient = client.resources(Release.class);
                  this.scClient = client.resources(StorageConfig.class);
                  this.backupClient = client.resources(Backup.class);

                  this.supportBundleClient = client.resources(SupportBundle.class);
                  SharedIndexInformer<YBUniverse> ybUniverseSharedIndexInformer;
                  SharedIndexInformer<Release> ybSoftwareReleaseIndexInformer;
                  SharedIndexInformer<StorageConfig> ybStorageConfigIndexInformer;
                  SharedIndexInformer<Backup> ybBackupIndexInformer;

                  SharedIndexInformer<SupportBundle> ybSupportBundleIndexInformer;
                  long resyncPeriodInMillis = 10 * 60 * 1000L;
                  SharedInformerFactory informerFactory = client.informers();
                  if (!namespace.trim().isEmpty()) {

                    // Listen to only one namespace.
                    ybUniverseSharedIndexInformer =
                        client
                            .resources(YBUniverse.class)
                            .inNamespace(namespace)
                            .inform(
                                new ResourceEventHandler<>() {
                                  @Override
                                  public void onAdd(YBUniverse Ybu) {}

                                  @Override
                                  public void onUpdate(YBUniverse Ybu, YBUniverse newYbu) {}

                                  @Override
                                  public void onDelete(
                                      YBUniverse Ybu, boolean deletedFinalUnknown) {}
                                },
                                resyncPeriodInMillis);
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
                  } else {
                    // Listen to all namespaces, use the factory to build informer.
                    ybUniverseSharedIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            YBUniverse.class, resyncPeriodInMillis);
                    ybSoftwareReleaseIndexInformer =
                        informerFactory.sharedIndexInformerFor(Release.class, resyncPeriodInMillis);
                    ybStorageConfigIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            StorageConfig.class, resyncPeriodInMillis);
                    ybBackupIndexInformer =
                        informerFactory.sharedIndexInformerFor(Backup.class, resyncPeriodInMillis);
                    ybSupportBundleIndexInformer =
                        informerFactory.sharedIndexInformerFor(
                            SupportBundle.class, resyncPeriodInMillis);
                  }
                  LOG.info("Finished setting up SharedIndexInformers");

                  KubernetesOperatorController ybUniverseController =
                      new KubernetesOperatorController(
                          client,
                          ybUniverseClient,
                          ybUniverseSharedIndexInformer,
                          namespace,
                          universeCRUDHandler,
                          upgradeUniverseHandler,
                          cloudProviderHandler,
                          kubernetesStatusUpdater);

                  ReleaseReconciler releaseReconciler =
                      new ReleaseReconciler(
                          ybSoftwareReleaseIndexInformer,
                          releasesClient,
                          releaseManager,
                          gFlagsValidation,
                          namespace);
                  SupportBundleReconciler supportBundleReconciler =
                      new SupportBundleReconciler(
                          ybSupportBundleIndexInformer,
                          supportBundleClient,
                          namespace,
                          commissioner,
                          taskExecutor,
                          supportBundleUtil);

                  StorageConfigReconciler scReconciler =
                      new StorageConfigReconciler(
                          ybStorageConfigIndexInformer, scClient, ccs, namespace);

                  BackupReconciler backupReconciler =
                      new BackupReconciler(
                          ybBackupIndexInformer,
                          backupClient,
                          backupHelper,
                          formFactory,
                          namespace,
                          ybStorageConfigIndexInformer);

                  Future<Void> startedInformersFuture =
                      informerFactory.startAllRegisteredInformers();

                  startedInformersFuture.get();
                  releaseReconciler.run();
                  scReconciler.run();
                  backupReconciler.run();
                  supportBundleReconciler.run();
                  ybUniverseController.run();

                  LOG.info("Finished running ybUniverseController");
                } catch (KubernetesClientException | ExecutionException exception) {
                  LOG.error("Kubernetes Client Exception : ", exception);
                } catch (InterruptedException interruptedException) {
                  LOG.error("Interrupted: ", interruptedException);
                  Thread.currentThread().interrupt();
                }
              } catch (Exception e) {
                LOG.error("Error", e);
              }
            });
    kubernetesOperatorThread.start();
  }
}
