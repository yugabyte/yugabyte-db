// // Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import io.fabric8.kubernetes.api.model.Namespaced;
import io.fabric8.kubernetes.client.CustomResource;
import io.fabric8.kubernetes.model.annotation.Group;
import io.fabric8.kubernetes.model.annotation.Version;
// import javax.annotation.processing;

// @Version("v1alpha1")
// @Group("operator.yugabyte.io")
// public class YBUniverse extends
// CustomResource<YBUniverseSpec,
//                YBUniverseStatus> implements Namespaced {}

// package com.yugabyte.yw.common.operator;

@io.fabric8.kubernetes.model.annotation.Version(value = "v1alpha1", storage = true, served = true)
@Group("operator.yugabyte.io")
@io.fabric8.kubernetes.model.annotation.Singular("YBUniverse")
@io.fabric8.kubernetes.model.annotation.Plural("YBUniverses")
@javax.annotation.Generated("io.fabric8.java.generator.CRGeneratorRunner")
public class YBUniverse extends CustomResource<YBUniverseSpec, YBUniverseStatus>
    implements Namespaced {}
