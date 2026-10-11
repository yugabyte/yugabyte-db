// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import java.util.EnumSet;
import java.util.List;
import org.mapstruct.Mapper;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

/**
 * Maps an incoming v2 create request onto the internal {@link SupportBundleFormDataV2}, which is
 * what the collection task persists in its task params.
 *
 * <p>Mapping onto a freshly constructed form rather than letting MapStruct instantiate the target
 * keeps the form's field defaults (core limits, Prometheus export mode) for anything the request
 * leaves out.
 */
@Mapper(
    config = CentralConfig.class,
    uses = {
      DateTimeMapper.class,
      SupportBundleComponentSpecMapper.class,
      SupportBundleEnumMapper.class
    })
public interface SupportBundleCreateSpecMapper {

  SupportBundleCreateSpecMapper INSTANCE = Mappers.getMapper(SupportBundleCreateSpecMapper.class);

  default SupportBundleFormDataV2 toSupportBundleFormData(SupportBundleCreateSpec source) {
    SupportBundleFormDataV2 formData = new SupportBundleFormDataV2();
    if (source != null) {
      fillSupportBundleFormData(source, formData);
    }
    return formData;
  }

  void fillSupportBundleFormData(
      SupportBundleCreateSpec source, @MappingTarget SupportBundleFormDataV2 target);

  default EnumSet<ComponentType> toComponentTypes(List<SupportBundleComponentType> source) {
    if (source == null) {
      return null;
    }
    EnumSet<ComponentType> components = EnumSet.noneOf(ComponentType.class);
    source.forEach(
        componentType ->
            components.add(SupportBundleEnumMapper.INSTANCE.toV1ComponentType(componentType)));
    return components;
  }

  default EnumSet<PrometheusMetricsType> toPrometheusMetricsTypes(
      List<api.v2.models.PrometheusMetricsType> source) {
    if (source == null) {
      return null;
    }
    EnumSet<PrometheusMetricsType> metricsTypes = EnumSet.noneOf(PrometheusMetricsType.class);
    source.forEach(
        metricsType ->
            metricsTypes.add(
                SupportBundleEnumMapper.INSTANCE.toV1PrometheusMetricsType(metricsType)));
    return metricsTypes;
  }
}
