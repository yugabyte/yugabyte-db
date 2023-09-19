package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import lombok.Getter;
import lombok.Setter;

public class KubernetesUniverseTaskParams extends UniverseTaskParams {
  @Getter @Setter private KubernetesResourceDetails kubernetesResourceDetails;
}
