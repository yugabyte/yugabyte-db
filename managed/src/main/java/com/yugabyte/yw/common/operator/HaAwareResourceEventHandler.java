// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import lombok.extern.slf4j.Slf4j;

/**
 * Single HA-follower gate for operator CR handling. Informers register {@link #wrap} so callbacks
 * never reach reconcilers. {@link AbstractReconciler} also calls {@link #skip} for work already on
 * the queue (backoff requeue from before demote), which does not go through the informer.
 */
@Slf4j
public final class HaAwareResourceEventHandler<T> implements ResourceEventHandler<T> {
  private final OperatorUtils operatorUtils;
  private final ResourceEventHandler<T> delegate;

  private HaAwareResourceEventHandler(
      OperatorUtils operatorUtils, ResourceEventHandler<T> delegate) {
    this.operatorUtils = operatorUtils;
    this.delegate = delegate;
  }

  public static <T> ResourceEventHandler<T> wrap(
      OperatorUtils operatorUtils, ResourceEventHandler<T> delegate) {
    return new HaAwareResourceEventHandler<>(operatorUtils, delegate);
  }

  /** True when this YBA is an HA follower and must not apply operator CRs. */
  public static boolean skip(OperatorUtils operatorUtils) {
    if (!operatorUtils.isHaFollower()) {
      return false;
    }
    log.debug("Skipping Kubernetes operator work on HA follower");
    return true;
  }

  @Override
  public void onAdd(T resource) {
    if (skip(operatorUtils)) {
      return;
    }
    delegate.onAdd(resource);
  }

  @Override
  public void onUpdate(T oldResource, T newResource) {
    if (skip(operatorUtils)) {
      return;
    }
    delegate.onUpdate(oldResource, newResource);
  }

  @Override
  public void onDelete(T resource, boolean deletedFinalStateUnknown) {
    if (skip(operatorUtils)) {
      return;
    }
    delegate.onDelete(resource, deletedFinalStateUnknown);
  }
}
