package com.yugabyte.yw.common.operator.utils;

public class KubernetesEnvironmentVariables {
  public static final String kubernetesServiceHost = "KUBERNETES_SERVICE_HOST";
  public static final String kubernetesServicePort = "KUBERNETES_SERVICE_PORT";

  public static String getServiceHost() {
    return System.getenv(KubernetesEnvironmentVariables.kubernetesServiceHost);
  }

  public static String getServicePort() {
    return System.getenv(KubernetesEnvironmentVariables.kubernetesServicePort);
  }
}
