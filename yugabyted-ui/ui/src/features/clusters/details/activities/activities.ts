import { useGetClusterActivitiesQuery } from "@app/api/src";
import { BadgeVariant } from "@app/components/YBBadge/YBBadge";
import { useMemo } from "react";

export const useActivities = () => {
  const { data: upstreamActivities } = useGetClusterActivitiesQuery({
    activities: "INDEX_BACKFILL",
  });

  /* const upstreamActivities = {
    data: [
        {
          name: "INDEX_BACKFILL",
          data: {
            Duration: "4.85 s",
            IndexName: "temp_index",
            Phase: "Completed",
            StartTime: "34.8 s ago",
          },
        },
      {
        name: "INDEX_BACKFILL",
        data: {
          Command: "CREATE INDEX CONCURRENTLY",
          DBName: "postgres",
          IndexName: "temp_index",
          PartitionsDone: 0,
          PartitionsTotal: 0,
          Phase: "backfilling",
          TuplesDone: 0,
          TuplesTotal: 156344,
        },
      },
    ],
  }; */

  const activityData = useMemo<any[]>(() => {
    return (
      upstreamActivities?.data.map((data) => ({
        name: data.name,
        status:
          (data.data as any).Phase === "Completed" ? BadgeVariant.Success : BadgeVariant.InProgress,
        ...data.data,
      })) ?? []
    );
  }, [upstreamActivities]);

  return activityData;
};
