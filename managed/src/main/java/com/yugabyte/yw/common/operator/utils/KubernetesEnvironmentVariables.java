package com.yugabyte.yw.common.operator.utils;

public class KubernetesEnvironmentVariables {
  public static final String kubernetesServiceHost = "KUBERNETES_SERVICE_HOST";
  public static final String kubernetesServicePort = "KUBERNETES_SERVICE_PORT";
  public static final String podName = "POD_NAME";
  public static final String podNamespace = "POD_NAMESPACE";
  public static final String pvcName = "PVC_NAME";

  public static String getServiceHost() {
    return System.getenv(KubernetesEnvironmentVariables.kubernetesServiceHost);
  }

  public static String getServicePort() {
    return System.getenv(KubernetesEnvironmentVariables.kubernetesServicePort);
  }

  public static String getPodName() {
    return System.getenv(KubernetesEnvironmentVariables.podName);
  }

  public static String getPodNamespace() {
    return System.getenv(KubernetesEnvironmentVariables.podNamespace);
  }

  public static String getPvcName() {
    return System.getenv(KubernetesEnvironmentVariables.pvcName);
  }

  public static boolean isYbaRunningInKubernetes() {
    return getServiceHost() != null && getServicePort() != null;
  }
}
