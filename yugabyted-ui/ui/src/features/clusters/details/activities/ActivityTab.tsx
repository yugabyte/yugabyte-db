import React, { FC } from "react";
import { Box, Grid, makeStyles, MenuItem, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBModal, YBSelect, YBButton } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { useActivities } from "./activities";
import { GetClusterTablesApiEnum, useGetClusterTablesQuery } from "@app/api/src";
import RefreshIcon from "@app/assets/refresh.svg";

const useStyles = makeStyles((theme) => ({
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
  },
  activityDetailBox: {
    padding: theme.spacing(2),
    marginTop: theme.spacing(1),
    marginBottom: theme.spacing(1.5),
    border: "1px solid #E9EEF2",
    borderRadius: theme.shape.borderRadius,
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginTop: theme.spacing(1),
    marginBottom: theme.spacing(0.5),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingBottom: theme.spacing(1),
    textAlign: "start",
  },
  selectBox: {
    minWidth: "200px",
  },
  sectionHeading: {
    fontWeight: 700,
    fontSize: "15px",
    color: theme.palette.grey[900],
    margin: theme.spacing(3, 0, 1, 0.5),
  },
}));

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
};

export const ActivityTab: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: clusterTablesResponseYsql, refetch: refetchYsql } = useGetClusterTablesQuery({
    api: GetClusterTablesApiEnum.Ysql,
  });
  const ysqlDBList = React.useMemo(
    () =>
      Array.from(
        (clusterTablesResponseYsql?.data ?? []).reduce(
          (prev, curr) => prev.add(curr.keyspace),
          new Set<string>()
        )
      ),
    [clusterTablesResponseYsql?.data]
  );

  const [selectedDB, setSelectedDB] = React.useState<string>(ysqlDBList[0] ?? "");
  React.useEffect(() => {
    if (
      (ysqlDBList.length > 0 && selectedDB === "") ||
      !ysqlDBList.find((db) => db === selectedDB)
    ) {
      setSelectedDB(ysqlDBList[0] ?? "");
    }
  }, [ysqlDBList]);

  const { data: inProgressActivityData, refetch: refetchInProgressActivities } = useActivities(
    "IN_PROGRESS",
    selectedDB
  );
  const { data: completedActivityData, refetch: refetchCompletedActivities } =
    useActivities("COMPLETED");

  const [drawerOpenData, setDrawerOpenData] = React.useState<{ dataList: any[]; index: number }>();

  const refetch = React.useCallback(() => {
    refetchInProgressActivities();
    refetchCompletedActivities();
    refetchYsql();
  }, [refetchInProgressActivities, refetchCompletedActivities, refetchYsql]);

  const activityColumns = [
    {
      name: "name",
      label: t("clusterDetail.activity.activity"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "Phase",
      label: t("clusterDetail.activity.status"),
      options: {
        customBodyRender: (phase: string) => {
          return (
            <YBBadge
              variant={phase === "Completed" ? BadgeVariant.Success : BadgeVariant.InProgress}
              text={phase}
            />
          );
        },
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "IndexName",
      label: t("clusterDetail.activity.index"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "",
      label: "",
      options: {
        sort: false,
        customBodyRender: ArrowComponent(classes),
      },
    },
  ];

  const activityValues = React.useMemo(() => {
    if (drawerOpenData === undefined) {
      return undefined;
    }

    return Object.entries<any>(drawerOpenData.dataList[drawerOpenData.index]);
  }, [drawerOpenData]);

  return (
    <Box>
      <Box className={classes.sectionHeading}>
        {t("clusterDetail.activity.inprogressActivities")}
      </Box>
      <Box
        display="flex"
        alignItems="end"
        justifyContent="space-between"
        pb={2}
        gridGap={4}
        flexWrap="wrap"
      >
        <Box>
          {ysqlDBList.length > 0 && (
            <>
              <Typography
                variant="subtitle2"
                className={classes.label}
                style={{ marginBottom: 0, marginLeft: 1.5 }}
              >
                {t("clusterDetail.activity.database")}
              </Typography>
              <YBSelect
                className={classes.selectBox}
                value={selectedDB}
                onChange={(e) => setSelectedDB(e.target.value)}
              >
                {ysqlDBList.map((dbName) => {
                  return (
                    <MenuItem key={dbName} value={dbName}>
                      {dbName}
                    </MenuItem>
                  );
                })}
              </YBSelect>
            </>
          )}
        </Box>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={refetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>
      {inProgressActivityData.length ? (
        <Box pb={5}>
          <YBTable
            data={inProgressActivityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) =>
                setDrawerOpenData({ dataList: inProgressActivityData, index: dataIndex }),
            }}
            touchBorder={false}
          />
        </Box>
      ) : (
        <YBLoadingBox>{t("clusterDetail.activity.noInprogressActivities")}</YBLoadingBox>
      )}

      <Box className={classes.sectionHeading}>
        {t("clusterDetail.activity.completedActivities")}
      </Box>
      {completedActivityData.length ? (
        <Box pb={4}>
          <YBTable
            data={completedActivityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) =>
                setDrawerOpenData({ dataList: completedActivityData, index: dataIndex }),
            }}
            touchBorder={false}
          />
        </Box>
      ) : (
        <YBLoadingBox>{t("clusterDetail.activity.noCompletedActivities")}</YBLoadingBox>
      )}

      <YBModal
        open={drawerOpenData !== undefined}
        title={t("clusterDetail.activity.details.title")}
        onClose={() => setDrawerOpenData(undefined)}
        enableBackdropDismiss
        titleSeparator
        cancelLabel={t("common.close")}
        isSidePanel
      >
        {drawerOpenData !== undefined && (
          <>
            <Box className={classes.activityDetailBox}>
              <Grid container spacing={2}>
                <Grid xs={6} item>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.activity.details.operationName")}
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    {drawerOpenData.dataList[drawerOpenData.index].name}
                  </Typography>
                </Grid>
                <Grid xs={6} item>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.activity.details.status")}
                  </Typography>
                  <YBBadge
                    variant={drawerOpenData.dataList[drawerOpenData.index].status}
                    text={drawerOpenData.dataList[drawerOpenData.index].Phase}
                  />
                </Grid>
                {activityValues?.map(([key, value]) =>
                  key === "name" || key === "status" ? (
                    <></>
                  ) : (
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {key.split(/(?=[A-Z][^A-Z])/).join(" ")}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {value}
                      </Typography>
                    </Grid>
                  )
                )}
                {/* <Grid xs={12} item>
                      <Box display="flex" justifyContent="space-between">
                        <Typography variant="subtitle2" className={classes.label}>
                          {t("clusterDetail.activity.details.progress")}
                        </Typography>
                        22%
                      </Box>
                      <YBProgress value={22} color={theme.palette.primary[500]} />
                    </Grid> */}
              </Grid>
            </Box>
          </>
        )}
      </YBModal>
    </Box>
  );
};
