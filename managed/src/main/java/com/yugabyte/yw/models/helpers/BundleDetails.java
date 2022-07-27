package com.yugabyte.yw.models.helpers;

import io.ebean.annotation.EnumValue;
import java.util.EnumSet;

public class BundleDetails {

  public enum ComponentType {
    @EnumValue("UniverseLogs")
    UniverseLogs,

    @EnumValue("ApplicationLogs")
    ApplicationLogs,

    @EnumValue("OutputFiles")
    OutputFiles,

    @EnumValue("ErrorFiles")
    ErrorFiles,

    @EnumValue("GFlags")
    GFlags,

    @EnumValue("Instance")
    Instance,

    @EnumValue("ConsensusMeta")
    ConsensusMeta,

    @EnumValue("TabletMeta")
    TabletMeta,

    @EnumValue("YbcLogs")
    YbcLogs;

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
}
