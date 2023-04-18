// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.SharedInformerFactory;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.fabric8.kubernetes.client.KubernetesClientException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import play.Logger;

/**
 * Main Class for Operator, you can run this sample using this command:
 *
 * <p>mvn exec:java -Dexec.mainClass= io.fabric8.ybUniverse.operator.YBUniverseOperatorMain
 */
public class KubernetesOperator {
  @Inject private UniverseCRUDHandler universeCRUDHandler;

  public MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;

  public void init() {
    Thread kubernetesOperatorThread =
        new Thread(
            () -> {
              try {
                Logger.info("Creating KubernetesOperator");
                try (KubernetesClient client = new KubernetesClientBuilder().build()) {
                  String namespace = client.getNamespace();
                  if (namespace == null) {
                    Logger.info("No namespace found via config, assuming default.");
                    namespace = "default";
                  }

                  Logger.info("Using namespace : {}", namespace);

                  SharedInformerFactory informerFactory = client.informers();

                  Logger.info("Finished setting up informers");

                  this.ybUniverseClient = client.resources(YBUniverse.class);
                  SharedIndexInformer<Pod> podSharedIndexInformer =
                      informerFactory.sharedIndexInformerFor(
                          Pod.class, 10 * 60 * 1000L
                          /* resyncPeriodInMillis */ );
                  SharedIndexInformer<YBUniverse> ybUniverseSharedIndexInformer =
                      informerFactory.sharedIndexInformerFor(
                          YBUniverse.class, 10 * 60 * 1000L
                          /* resyncPeriodInMillis */ );

                  Logger.info("Finished setting up SharedIndexInformers");

                  KubernetesOperatorController ybUniverseController =
                      new KubernetesOperatorController(
                          ybUniverseClient,
                          ybUniverseSharedIndexInformer,
                          namespace,
                          universeCRUDHandler);

                  Future<Void> startedInformersFuture =
                      informerFactory.startAllRegisteredInformers();

                  startedInformersFuture.get();

                  ybUniverseController.run();

                  Logger.info("Finished running ybUniverseController");

                } catch (KubernetesClientException | ExecutionException exception) {
                  Logger.error("Kubernetes Client Exception : ", exception);
                } catch (InterruptedException interruptedException) {
                  Logger.error("Interrupted: ", interruptedException);
                  Thread.currentThread().interrupt();
                }
              } catch (Exception e) {
                Logger.error("Error", e);
              }
            });
    kubernetesOperatorThread.start();
  }
}
