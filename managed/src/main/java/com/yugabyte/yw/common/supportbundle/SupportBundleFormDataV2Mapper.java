// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import java.util.EnumSet;
import java.util.Map;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.factory.Mappers;

/**
 * Maps the v2 support bundle form onto the v1 form consumed by the component collection layer.
 *
 * <p>The v2 form is a superset of the v1 form, so every v1 field is filled from its same-named v2
 * source. {@code unmappedTargetPolicy = ERROR} turns a v1 field with no v2 counterpart into a
 * compile error instead of a silently dropped value. The v2-only component spec lists have no v1
 * target and are dropped.
 */
@Mapper(unmappedTargetPolicy = ReportingPolicy.ERROR)
public interface SupportBundleFormDataV2Mapper {

  SupportBundleFormDataV2Mapper INSTANCE = Mappers.getMapper(SupportBundleFormDataV2Mapper.class);

  SupportBundleFormData toV1FormData(SupportBundleFormDataV2 v2Form);

  // The collection fields are handed over as-is. Without these, MapStruct re-instantiates them,
  // which loses the EnumSet implementation the collection layer relies on.

  default EnumSet<ComponentType> mapComponents(EnumSet<ComponentType> components) {
    return components;
  }

  default EnumSet<PrometheusMetricsType> mapMetricsTypes(EnumSet<PrometheusMetricsType> types) {
    return types;
  }

  default Map<String, String> mapPromQueries(Map<String, String> promQueries) {
    return promQueries;
  }
}
