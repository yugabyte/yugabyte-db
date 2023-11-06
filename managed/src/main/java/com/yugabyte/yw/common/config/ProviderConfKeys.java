/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;

public class ProviderConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Boolean> allowUnsupportedInstances =
      new ConfKeyInfo<>(
          "yb.internal.allow_unsupported_instances",
          ScopeType.PROVIDER,
          "Allow Unsupported Instances",
          "Enabling removes supported instance type filtering on AWS providers.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultAwsInstanceType =
      new ConfKeyInfo<>(
          "yb.aws.default_instance_type",
          ScopeType.PROVIDER,
          "Default AWS Instance Type",
          "Default AWS Instance Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultGcpInstanceType =
      new ConfKeyInfo<>(
          "yb.gcp.default_instance_type",
          ScopeType.PROVIDER,
          "Default GCP Instance Type",
          "Default GCP Instance Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultAzureInstanceType =
      new ConfKeyInfo<>(
          "yb.azure.default_instance_type",
          ScopeType.PROVIDER,
          "Default Azure Instance Type",
          "Default Azure Instance Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultKubInstanceType =
      new ConfKeyInfo<>(
          "yb.kubernetes.default_instance_type",
          ScopeType.PROVIDER,
          "Default Kubernetes Instance Type",
          "Default Kubernetes Instance Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultAwsStorageType =
      new ConfKeyInfo<>(
          "yb.aws.storage.default_storage_type",
          ScopeType.PROVIDER,
          "Default AWS Storage Type",
          "Default AWS Storage Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultGcpStorageType =
      new ConfKeyInfo<>(
          "yb.gcp.storage.default_storage_type",
          ScopeType.PROVIDER,
          "Default GCP Storage Type",
          "Default GCP Storage Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> defaultAzureStorageType =
      new ConfKeyInfo<>(
          "yb.azure.storage.default_storage_type",
          ScopeType.PROVIDER,
          "Default Azure Storage Type",
          "Default Azure Storage Type",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> universeBootScript =
      new ConfKeyInfo<>(
          "yb.universe_boot_script",
          ScopeType.PROVIDER,
          "Universe Boot Script",
          "Custom script to run on VM boot during universe provisioning",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> minPyVer =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.min_python_version",
          ScopeType.PROVIDER,
          "Min Python Version (inclusive)",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<String> maxPyVer =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.max_python_version",
          ScopeType.PROVIDER,
          "Max Python Version (exclusive)",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> defaultAwsVolumeCount =
      new ConfKeyInfo<>(
          "yb.aws.default_volume_count",
          ScopeType.PROVIDER,
          "Default AWS Volume Count",
          "Default AWS Volume Count",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultAwsVolumeSize =
      new ConfKeyInfo<>(
          "yb.aws.default_volume_size_gb",
          ScopeType.PROVIDER,
          "Default AWS Volume Size",
          "Default AWS Volume Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultGcpVolumeSize =
      new ConfKeyInfo<>(
          "yb.gcp.default_volume_size_gb",
          ScopeType.PROVIDER,
          "Default GCP Volume Size",
          "Default GCP Volume Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultAzureVolumeSize =
      new ConfKeyInfo<>(
          "yb.azure.default_volume_size_gb",
          ScopeType.PROVIDER,
          "Default Azure Volume Size",
          "Default Azure Volume Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultKubernetesVolumeCount =
      new ConfKeyInfo<>(
          "yb.kubernetes.default_volume_count",
          ScopeType.PROVIDER,
          "Default Kubernetes Volume Count",
          "Default Kubernetes Volume Count",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultKubernetesVolumeSize =
      new ConfKeyInfo<>(
          "yb.kubernetes.default_volume_size_gb",
          ScopeType.PROVIDER,
          "Default Kubernetes Volume Size",
          "Default Kubernetes Volume Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultKubernetesCpuCores =
      new ConfKeyInfo<>(
          "yb.kubernetes.default_cpu_cores",
          ScopeType.PROVIDER,
          "Default Kubernetes CPU cores",
          "Default Kubernetes CPU cores",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> minKubernetesCpuCores =
      new ConfKeyInfo<>(
          "yb.kubernetes.min_cpu_cores",
          ScopeType.PROVIDER,
          "Minimum Kubernetes CPU cores",
          "Minimum Kubernetes CPU cores",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> maxKubernetesCpuCores =
      new ConfKeyInfo<>(
          "yb.kubernetes.max_cpu_cores",
          ScopeType.PROVIDER,
          "Maximum Kubernetes CPU cores",
          "Maximum Kubernetes CPU cores",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> defaultKubernetesMemorySize =
      new ConfKeyInfo<>(
          "yb.kubernetes.default_memory_size_gb",
          ScopeType.PROVIDER,
          "Default Kubernetes Memory Size",
          "Default Kubernetes Memory Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> minKubernetesMemorySize =
      new ConfKeyInfo<>(
          "yb.kubernetes.min_memory_size_gb",
          ScopeType.PROVIDER,
          "Minimum Kubernetes Memory Size",
          "Minimum Kubernetes Memory Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> maxKubernetesMemorySize =
      new ConfKeyInfo<>(
          "yb.kubernetes.max_memory_size_gb",
          ScopeType.PROVIDER,
          "Maximum Kubernetes Memory Size",
          "Maximum Kubernetes Memory Size",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> user =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.user",
          ScopeType.PROVIDER,
          "User",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<String> userGroup =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.user_group",
          ScopeType.PROVIDER,
          "User Group",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> minPrometheusSpaceMb =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.min_prometheus_space_mb",
          ScopeType.PROVIDER,
          "Min Prometheus Space MB",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> minTempDirSpaceMb =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.min_tmp_dir_space_mb",
          ScopeType.PROVIDER,
          "Min Temp Dir Space MB",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> minHomeDirSpaceMb =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.min_home_dir_space_mb",
          ScopeType.PROVIDER,
          "Min Home Space MB",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> minMountPointDirSpaceMb =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.min_mount_point_dir_space_mb",
          ScopeType.PROVIDER,
          "Min Mount Point Dir Space MB",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<String> ulimitCore =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.ulimit_core",
          ScopeType.PROVIDER,
          "ulimit core ",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<String> ulimitOpenFiles =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.ulimit_open_files",
          ScopeType.PROVIDER,
          "ulimit open files",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<String> ulimitUserProcesses =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.ulimit_user_processes",
          ScopeType.PROVIDER,
          "ulimit user processes",
          "",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> swappiness =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.swappiness",
          ScopeType.PROVIDER,
          "swappiness",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Integer> sshTimeout =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.ssh_timeout",
          ScopeType.PROVIDER,
          "SSH connection timeout",
          "",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));

  public static final ConfKeyInfo<Boolean> enableNodeAgentClient =
      new ConfKeyInfo<>(
          "yb.node_agent.client.enabled",
          ScopeType.PROVIDER,
          "Enable Node Agent Client",
          "Enable node agent client for communication to DB nodes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Integer> vmMaxMemCount =
      new ConfKeyInfo<>(
          "yb.node_agent.preflight_checks.vm_max_map_count",
          ScopeType.PROVIDER,
          "VM max map count",
          "Max count of memory-mapped regions allowed in the system.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Boolean> enableAnsibleOffloading =
      new ConfKeyInfo<>(
          "yb.node_agent.ansible_offloading.enabled",
          ScopeType.PROVIDER,
          "Enable Ansible Offloading",
          "Offload ansible tasks to the DB nodes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> useSpotInstances =
      new ConfKeyInfo<>(
          "yb.use_spot_instances",
          ScopeType.PROVIDER,
          "Use Spot Instances",
          "Use spot instances instead of On-Demand during universe creation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> enableYbcOnK8s =
      new ConfKeyInfo<>(
          "ybc.k8s.enabled",
          ScopeType.PROVIDER,
          "Enable YBC on K8S",
          "To enable ybc on k8s universe",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Boolean> ybcListenOnAllInterfacesK8s =
      new ConfKeyInfo<>(
          "yb.ybc_flags.listen_on_all_interfaces_k8s",
          ScopeType.PROVIDER,
          "Make YBC listen on 0.0.0.0",
          "Makes YBC bind on all network interfaces",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
}
