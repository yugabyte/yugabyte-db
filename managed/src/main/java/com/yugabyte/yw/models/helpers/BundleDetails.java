package com.yugabyte.yw.models.helpers;

import java.util.List;

public class BundleDetails {

  public List<String> components;

  public BundleDetails() {}

  public BundleDetails(List<String> components) {
    this.components = components;
  }
}
