// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import static com.yugabyte.yw.cloud.PublicCloudConstants.OCI_BLOCK_STORAGE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.OCI_BLOCK_VPU;
import static com.yugabyte.yw.cloud.PublicCloudConstants.OCI_COMPUTE_MEMORY_FORMAT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.OCI_COMPUTE_OCPU_FORMAT;

import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import javax.annotation.Nullable;

/** Helpers for OCI list-price meters and shape to compute-family mapping. */
public final class OCIPriceUtil {

  public static final String PRICELIST_RESOURCE = "oci_pricing/pricelist.json";
  public static final double HOURS_PER_MONTH = 24.0 * 30.0;

  /** Default VPUs/GB for YBA OCI storage types (matches ybops create_volume defaults). */
  public static final int VPUS_PER_GB_HIGHER_PERFORMANCE = 20;

  public static final int VPUS_PER_GB_LOWER_COST = 0;
  public static final int VPUS_PER_GB_BALANCED = 10;

  private OCIPriceUtil() {}

  public static String ocpuComponentCode(String family) {
    return String.format(OCI_COMPUTE_OCPU_FORMAT, family);
  }

  public static String memoryComponentCode(String family) {
    return String.format(OCI_COMPUTE_MEMORY_FORMAT, family);
  }

  public static String blockStorageComponentCode() {
    return OCI_BLOCK_STORAGE;
  }

  public static String blockVpuComponentCode() {
    return OCI_BLOCK_VPU;
  }

  /**
   * Maps an OCI shape name to a compute family key used in the bundled pricelist / PriceComponent
   * meters. Returns null when the shape is unsupported for pricing (e.g. DenseIO / GPU).
   */
  @Nullable
  public static String familyFromShape(@Nullable String shape) {
    if (shape == null || shape.isEmpty()) {
      return null;
    }
    String s = shape;
    // Dense I/O / GPU are out of scope for v1.
    if (s.contains("DenseIO") || s.contains("GPU") || s.contains("HV.")) {
      return null;
    }
    // Intel gen-2/gen-3 naming does not embed the SKU family letter (X7 / X9).
    if (s.contains("Standard2")) {
      return "X7";
    }
    if (s.contains("Standard3")) {
      return "X9";
    }
    if (s.contains("Optimized3")) {
      return "OptimizedX9";
    }
    // E2 Micro is metered separately from E2.
    if (s.contains("E2") && s.contains("Micro")) {
      return "E2Micro";
    }

    // VM.Standard.E4.Flex maps to  E4, VM.Standard.A1.Flex maps to A1, BM.Standard.E4.128 to E4
    for (String part : s.split("\\.")) {
      if (part.matches("^[A-Z]\\d+$")) {
        return part;
      }
    }
    return null;
  }

  public static int vpusPerGb(StorageType storageType) {
    if (storageType == null) {
      return VPUS_PER_GB_BALANCED;
    }
    switch (storageType) {
      case OCI_HigherPerformance:
        return VPUS_PER_GB_HIGHER_PERFORMANCE;
      case OCI_LowerCost:
        return VPUS_PER_GB_LOWER_COST;
      case OCI_Balanced:
      default:
        return VPUS_PER_GB_BALANCED;
    }
  }

  public static double monthlyToHourly(double pricePerMonth) {
    return pricePerMonth / HOURS_PER_MONTH;
  }
}
