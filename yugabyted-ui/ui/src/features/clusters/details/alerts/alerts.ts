import { AXIOS_INSTANCE } from "@app/api/src";
import { BadgeVariant } from "@app/components/YBBadge/YBBadge";
import { getUnixTime } from "date-fns";
import { subMinutes } from "date-fns";
import { useEffect, useState } from "react";

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

export const useCPUAlert = () => {
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
          `&start_time=${getUnixTime(interval.start)}&end_time=${getUnixTime(interval.end)}`
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
  }, []);

  return cpuAlerts;
};
