// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import java.util.ArrayList;

public class UniverseResourceDetails {
  public double pricePerHour = 0;
  public int numCores = 0;
  public double memSizeGB = 0;
  public int volumeCount = 0;
  public int volumeSizeGB = 0;
  public String volumeType = "";
  public ArrayList<String> azList = new ArrayList<String>();

  public void addCostPerHour(double price) {
    pricePerHour += price;
  }

  public void addVolumeCount(double volCount) {
    volumeCount += volCount;
  }

  public void addmemSizeGB(double memSize) {
    memSizeGB += memSize;
  }

  public void addvolumeSizeGB(double volSize) {
    volumeSizeGB += volSize;
  }

  public void addVolumeType(String type) {
    volumeType = type;
  }

  public void addAz(String azName) {
    azList.add(azName);
  }

  public void addNumCores(int cores) {
    numCores += cores;
  }
}
