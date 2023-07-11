import { AXIOS_INSTANCE, AlertsResponse, useGetClusterAlertsQuery } from "@app/api/src";
import { BadgeVariant } from "@app/components/YBBadge/YBBadge";
import axios from "axios";
import { getUnixTime } from "date-fns";
import { subMinutes } from "date-fns";
import { useEffect, useMemo, useState } from "react";
import { useLocalStorage } from "react-use";

export const alertConfigurationsKey = "alert-configurations";

export const alertList = [
  {
    name: "Cluster CPU utilization exceeded 90% for 5 min",
    key: "cpu_90",
    status: BadgeVariant.Error,
  },
  {
    name: "Cluster CPU utilization exceeded 75% for 5 min",
    key: "cpu_75",
    status: BadgeVariant.Warning,
  },
  {
    name: "Low open file ulimit",
    key: "open_files",
    status: BadgeVariant.Warning,
  },
  {
    name: "Low max user processes ulimit",
    key: "max_user_processes",
    status: BadgeVariant.Warning,
  },
  {
    name: "Transparent hugepages disabled",
    key: "transparent_hugepages",
    status: BadgeVariant.Warning,
  },
  {
    name: "Missing ntp/chrony package for clock synchronization",
    key: "ntp/chrony",
    status: BadgeVariant.Warning,
  },
  {
    name: "Insecure cluster",
    key: "insecure",
    status: BadgeVariant.Warning,
  },
] as const;

export type AlertConfiguration = {
  key: string;
  enabled: boolean;
};

type CPUAlert = {
  cpu_90: boolean;
  cpu_75: boolean;
};

export const useFetchAlerts = (nodeHost: string | true = "") => {
  const { data: upstreamAlerts, refetch: refetchUpstreamAlerts } = 
    useGetClusterAlertsQuery({ query: { enabled: nodeHost === true }});

  const [refetch, setRefetch] = useState<boolean>(false);
  const refetchAlerts = () => setRefetch((prev) => !prev);

  const [alerts, setAlerts] = useState<AlertsResponse>({
    data: [],
  });

  useEffect(() => {
    if (nodeHost === true) {
      return;
    } else if (!nodeHost) {
      setAlerts({ data: [] });
      return;
    }

    const fetchNodeAlerts = async () => {
      const alerts = await axios.get<AlertsResponse>(`http://${nodeHost}:15433/api/alerts`)
        .then(({ data }) => data)
        .catch((err) => {
          console.error(err);
          return { data: [] } as AlertsResponse;
        });

      setAlerts(alerts);
    };

    fetchNodeAlerts();
  }, [nodeHost, refetch]);



  return {
    data: nodeHost === true ? upstreamAlerts : alerts, 
    refetch: nodeHost === true ? refetchUpstreamAlerts : refetchAlerts
  };
};

export const useCPUAlert = (nodeHost: string = "") => {
  const [refetch, setRefetch] = useState<boolean>(false);
  const refetchAlerts = () => setRefetch((prev) => !prev);

  const [cpuAlerts, setCpuAlerts] = useState<CPUAlert>({
    cpu_90: false,
    cpu_75: false,
  });

  useEffect(() => {
    const computeCpuAlerts = async () => {
      const end = new Date();
      const interval = { start: subMinutes(end, 5), end };
      const cpuAlerts = await AXIOS_INSTANCE.get(
        `/metrics?metrics=CPU_USAGE_SYSTEM%2CCPU_USAGE_USER` +
          `&start_time=${getUnixTime(interval.start)}&end_time=${getUnixTime(
            interval.end
          )}&node_name=${nodeHost}`
      )
        .then(({ data }) => {
          const cpuUsages = {
            system: data.data[0].values.filter((val: any[]) => val.length === 2) as [
              number,
              number
            ][],
            user: data.data[1].values.filter((val: any[]) => val.length === 2) as [
              number,
              number
            ][],
          };

          const timestampList = new Set<number>();
          cpuUsages.system.forEach((cpu) => timestampList.add(cpu[0]));
          cpuUsages.user.forEach((cpu) => timestampList.add(cpu[0]));

          const cpuUsageList = Array.from(timestampList).map((timestamp) => {
            const system = cpuUsages.system.find((cpu) => cpu[0] === timestamp)?.[1] || 0;
            const user = cpuUsages.user.find((cpu) => cpu[0] === timestamp)?.[1] || 0;
            return system + user;
          });

          return {
            cpu_90: cpuUsageList.every((cpuUsage) => cpuUsage > 90),
            cpu_75: cpuUsageList.every((cpuUsage) => cpuUsage > 75),
          };
        })
        .catch((err) => {
          console.error(err);
          return {
            cpu_90: false,
            cpu_75: false,
          };
        });

      setCpuAlerts(cpuAlerts);
    };

    computeCpuAlerts();
  }, [nodeHost, refetch]);

  return { data: cpuAlerts, refetch: refetchAlerts };
};

export type AlertNotification = {
  key: string;
  title: string;
  info: string;
  status: BadgeVariant;
};

export const useAlerts = (nodeHost: string | true = "") => {
  const { data: upstreamAlerts, refetch: refetchUpstreamAlerts } = useFetchAlerts(nodeHost);

  /* const upstreamAlerts = {
    data: [
      {
        name: "ntp/chrony",
        info: "ntp/chrony package is missing for clock synchronization. For centos 7, we recommend installing either ntp or chrony package and for centos 8, we recommend installing chrony package.",
      },
      {
        name: "insecure",
        info: "Cluster started in an insecure mode without authentication and encryption enabled. For non-production use only, not to be used without firewalls blocking the internet traffic.",
      },
    ],
  }; */

  const { data: cpuAlerts, refetch: refetchCPUAlerts } = 
    useCPUAlert(nodeHost === true ? undefined : nodeHost);

  const refetch = () => {
    refetchUpstreamAlerts();
    refetchCPUAlerts();
  }

  const upstreamNotificationData = useMemo(() => upstreamAlerts?.data || [], [upstreamAlerts]);

  const alertData = useMemo<AlertNotification[]>(() => {
    const cpuNotifications = [];
    if (cpuAlerts.cpu_90) {
      const alert = alertList.find((alert) => alert.key === Object.keys(cpuAlerts)[0])!;
      cpuNotifications.push({
        key: alert.key,
        title: "Very high CPU utilization",
        info: alert.name,
        status: BadgeVariant.Error,
      });
    } else if (cpuAlerts.cpu_75) {
      const alert = alertList.find((alert) => alert.key === Object.keys(cpuAlerts)[1])!;
      cpuNotifications.push({
        key: alert.key,
        title: "High CPU utilization",
        info: alert.name,
        status: BadgeVariant.Warning,
      });
    }

    const apiNotifications = upstreamNotificationData
      .map((notificationData) => ({
        key: notificationData.name,
        title:
          alertList.find((alert) => alert.key === notificationData.name)?.name ||
          BadgeVariant.Warning,
        info: notificationData.info,
        status: BadgeVariant.Warning,
      }));

    return [...cpuNotifications, ...apiNotifications];
  }, [upstreamNotificationData, cpuAlerts, nodeHost]);

  const [config] = useLocalStorage<AlertConfiguration[]>(alertConfigurationsKey);

  const configuredAlertData = useMemo<AlertNotification[]>(
    () =>
      alertData.filter(
        (notification) => config?.find((config) => config.key === notification.key)?.enabled ?? true
      ),
    [alertData, config]
  );

  return { data: configuredAlertData, refetch };
};
