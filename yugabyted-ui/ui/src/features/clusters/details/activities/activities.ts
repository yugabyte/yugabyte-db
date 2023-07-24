import { useGetClusterActivitiesQuery } from "@app/api/src";
import { BadgeVariant } from "@app/components/YBBadge/YBBadge";
import { useMemo } from "react";

type ACTIVITY_STATUS = "IN_PROGRESS" | "COMPLETED";

export const useActivities = (status: ACTIVITY_STATUS, database?: string) => {
  const { data: upstreamActivities, refetch } = useGetClusterActivitiesQuery(
    {
      activities: "INDEX_BACKFILL",
      status,
      database,
    },
    { query: { enabled: (status === "IN_PROGRESS" && !!database) || status === "COMPLETED" } }
  );

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
      upstreamActivities?.data.map((data) => {
        const activityData = data.data as any;
        const isCompletedActivity = activityData.Phase === "Completed";

        return {
          Name: data.name,
          status: isCompletedActivity ? BadgeVariant.Success : BadgeVariant.InProgress,
          ...(isCompletedActivity
            ? activityData
            : {
                Command: activityData.Command,
                DBName: activityData.DBName,
                IndexName: activityData.IndexName,
                tuplesTotal: activityData.TuplesTotal,
                tuplesDone: activityData.TuplesDone,
                progress:
                  activityData.TuplesDone !== undefined && activityData.TuplesTotal
                    ? Math.round(activityData.TuplesDone / activityData.TuplesTotal)
                    : 0,
              }),
        };
      }) ?? []
    );
  }, [upstreamActivities]);

  return { data: activityData, refetch };
};
