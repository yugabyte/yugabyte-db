package com.yugabyte.yw.common.operator;

// import io.fabric8.kubernetes.api.model.HasMetadata;
import io.fabric8.kubernetes.client.CustomResource;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;

public abstract class AbstractReconciler<T extends CustomResource<?, ?>>
    implements ResourceEventHandler<T> {
  protected final KubernetesClient client;
  protected final YBInformerFactory informerFactory;

  public AbstractReconciler(KubernetesClient client, YBInformerFactory informerFactory) {
    this.client = client;
    this.informerFactory = informerFactory;
  }

  // Handle add, update, and delete from the informer
  public abstract void onAdd(T resource);

  public abstract void onUpdate(T oldResource, T newResource);

  public abstract void onDelete(T resource, boolean b);
}
