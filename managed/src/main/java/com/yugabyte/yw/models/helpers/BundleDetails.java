package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.annotation.EnumValue;
import java.util.EnumSet;
import java.util.stream.Collectors;

public class BundleDetails {

  public enum ComponentLevel {
    NodeLevel,
    GlobalLevel;
  }

  public enum ComponentType {
    @EnumValue("UniverseLogs")
    UniverseLogs(ComponentLevel.NodeLevel),

    @EnumValue("OutputFiles")
    OutputFiles(ComponentLevel.NodeLevel),

    @EnumValue("ErrorFiles")
    ErrorFiles(ComponentLevel.NodeLevel),

    @EnumValue("CoreFiles")
    CoreFiles(ComponentLevel.NodeLevel),

    @EnumValue("GFlags")
    GFlags(ComponentLevel.NodeLevel),

    @EnumValue("Instance")
    Instance(ComponentLevel.NodeLevel),

    @EnumValue("ConsensusMeta")
    ConsensusMeta(ComponentLevel.NodeLevel),

    @EnumValue("TabletMeta")
    TabletMeta(ComponentLevel.NodeLevel),

    @EnumValue("YbcLogs")
    YbcLogs(ComponentLevel.NodeLevel),

    @EnumValue("K8sInfo")
    K8sInfo(ComponentLevel.GlobalLevel),

    @EnumValue("NodeAgent")
    NodeAgent(ComponentLevel.NodeLevel),

    @EnumValue("YbaMetadata")
    YbaMetadata(ComponentLevel.GlobalLevel),

    // Add any new components above this component, as we want this to be the last collected
    // component to debug any issues with support bundle itself.
    @EnumValue("ApplicationLogs")
    ApplicationLogs(ComponentLevel.GlobalLevel);

    private final ComponentLevel componentLevel;

    ComponentType(ComponentLevel componentLevel) {
      this.componentLevel = componentLevel;
    }

    public ComponentLevel getComponentLevel() {
      return this.componentLevel;
    }

    public static boolean isValid(String type) {
      for (ComponentType t : ComponentType.values()) {
        if (t.name().equals(type)) {
          return true;
        }
      }

      return false;
    }
  }

  public EnumSet<ComponentType> components;

  public BundleDetails() {}

  public BundleDetails(EnumSet<ComponentType> components) {
    this.components = components;
  }

  @JsonIgnore
  public EnumSet<ComponentType> getNodeLevelComponents() {
    return this.components.stream()
        .filter(ct -> ComponentLevel.NodeLevel.equals(ct.getComponentLevel()))
        .collect(Collectors.toCollection(() -> EnumSet.noneOf(ComponentType.class)));
  }

  @JsonIgnore
  public EnumSet<ComponentType> getGlobalLevelComponents() {
    return this.components.stream()
        .filter(ct -> ComponentLevel.GlobalLevel.equals(ct.getComponentLevel()))
        .collect(Collectors.toCollection(() -> EnumSet.noneOf(ComponentType.class)));
  }
}
