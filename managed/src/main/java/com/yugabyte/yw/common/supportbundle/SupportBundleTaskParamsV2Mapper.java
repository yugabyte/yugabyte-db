// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParamsV2;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.Universe;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.factory.Mappers;

/**
 * Maps v2 support bundle task params onto the v1 params consumed by the component collection layer.
 *
 * <p>{@code unmappedTargetPolicy = ERROR} turns a v1 target property with no v2 source into a
 * compile error instead of a silently null field.
 */
@Mapper(unmappedTargetPolicy = ReportingPolicy.ERROR, uses = SupportBundleFormDataV2Mapper.class)
public interface SupportBundleTaskParamsV2Mapper {

  SupportBundleTaskParamsV2Mapper INSTANCE =
      Mappers.getMapper(SupportBundleTaskParamsV2Mapper.class);

  // Retry bookkeeping inherited from AbstractTaskParams belongs to the task that is actually
  // running, not to the derived v1 view of its params.
  @Mapping(target = "errorString", ignore = true)
  @Mapping(target = "previousTaskUUID", ignore = true)
  SupportBundleTaskParams toV1TaskParams(SupportBundleTaskParamsV2 v2Params);

  /**
   * Builds an unpersisted v1 bundle mirroring v2 state, for the component collection layer.
   *
   * <p>Written by hand rather than generated: all but two of the v1 fields are settable only
   * through the 7-argument constructor, and letting MapStruct drive the entity would also auto-map
   * {@code pathObject}, whose setter dereferences its argument while the path is still unset here.
   */
  default SupportBundle toV1SupportBundle(SupportBundleV2 v2Bundle) {
    if (v2Bundle == null) {
      return null;
    }
    return new SupportBundle(
        v2Bundle.getBundleUUID(),
        v2Bundle.getScopeUUID(),
        v2Bundle.getPath(),
        v2Bundle.getStartDate(),
        v2Bundle.getEndDate(),
        v2Bundle.getBundleDetails(),
        toV1Status(v2Bundle.getStatus()));
  }

  /** Generated, so a v2 status with no v1 counterpart fails the build rather than a bundle run. */
  SupportBundleStatusType toV1Status(SupportBundleV2StatusType v2Status);

  // Handed over as-is. Without these, MapStruct synthesizes deep copies of the Ebean entities where
  // the collection layer expects the same instances the task is already working with.

  default Customer mapCustomer(Customer customer) {
    return customer;
  }

  default Universe mapUniverse(Universe universe) {
    return universe;
  }

  default KubernetesResourceDetails mapKubernetesResourceDetails(
      KubernetesResourceDetails resourceDetails) {
    return resourceDetails;
  }
}
