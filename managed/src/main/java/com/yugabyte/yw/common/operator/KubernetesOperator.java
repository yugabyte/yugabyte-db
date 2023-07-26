// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.fabric8.kubernetes.client.KubernetesClientException;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.SharedInformerFactory;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.StorageConfig;
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
  @Inject private CustomerConfigService ccs;

  public MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;

  public MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> releasesClient;
  public MixedOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      scClient;
  public static final Logger LOG = LoggerFactory.getLogger(KubernetesOperator.class);

  public void init(String namespace) {
    LOG.info("Creating KubernetesOperator thread");
    Thread kubernetesOperatorThread =
        new Thread(
            () -> {
              try {
                long startTime = System.currentTimeMillis();
                LOG.info("Creating KubernetesOperator");
                try (KubernetesClient client = new KubernetesClientBuilder().build()) {
                  // maybe use: try (KubernetesClient client = getClient(config))
                  LOG.info("Using namespace : {}", namespace);

                  this.ybUniverseClient = client.resources(YBUniverse.class);
                  this.releasesClient = client.resources(Release.class);
                  this.scClient = client.resources(StorageConfig.class);

                  SharedIndexInformer<YBUniverse> ybUniverseSharedIndexInformer;
                  SharedIndexInformer<Release> ybSoftwareReleaseIndexInformer;
                  SharedIndexInformer<StorageConfig> ybStorageConfigIndexInformer;

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
                          cloudProviderHandler);

                  ReleaseReconciler releaseReconciler =
                      new ReleaseReconciler(
                          ybSoftwareReleaseIndexInformer, releasesClient, releaseManager);

                  StorageConfigReconciler scReconciler =
                      new StorageConfigReconciler(
                          ybStorageConfigIndexInformer, scClient, ccs, namespace);

                  Future<Void> startedInformersFuture =
                      informerFactory.startAllRegisteredInformers();

                  startedInformersFuture.get();
                  releaseReconciler.run();
                  scReconciler.run();
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
