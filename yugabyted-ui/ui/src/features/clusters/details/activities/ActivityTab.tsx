import React, { FC } from "react";
import { Box, Grid, makeStyles, MenuItem, Paper, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBModal, YBSelect, YBButton, YBProgress } from "@app/components";
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
  selectBoxWrapper: {
    margin: theme.spacing(-1, 0, 1.5, 0),
  },
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    marginBottom: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: "100%",
  },
  heading: {
    marginBottom: theme.spacing(5),
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

  const theme = useTheme();

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
      name: "Name",
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
      <Paper className={classes.paperContainer}>
        <Box display="flex" justifyContent="space-between" alignItems="start">
          <Typography variant="h4" className={classes.heading}>
            {t("clusterDetail.activity.inprogressActivities")}
          </Typography>
          <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={refetch}>
            {t("clusterDetail.performance.actions.refresh")}
          </YBButton>
        </Box>
        {ysqlDBList.length > 0 && (
          <Box className={classes.selectBoxWrapper}>
            <Typography
              variant="subtitle2"
              className={classes.label}
              style={{ marginBottom: 0, marginLeft: 1.5, marginTop: 0 }}
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
          </Box>
        )}
        {inProgressActivityData.length ? (
          <YBTable
            data={inProgressActivityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) =>
                setDrawerOpenData({ dataList: inProgressActivityData, index: dataIndex }),
            }}
            withBorder={false}
          />
        ) : (
          <YBLoadingBox>{t("clusterDetail.activity.noInprogressActivities")}</YBLoadingBox>
        )}
      </Paper>

      <Paper className={classes.paperContainer}>
        <Typography variant="h4" className={classes.heading}>
          {t("clusterDetail.activity.completedActivities")}
        </Typography>
        {completedActivityData.length ? (
          <YBTable
            data={completedActivityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) =>
                setDrawerOpenData({ dataList: completedActivityData, index: dataIndex }),
            }}
            withBorder={false}
          />
        ) : (
          <YBLoadingBox>{t("clusterDetail.activity.noCompletedActivities")}</YBLoadingBox>
        )}
      </Paper>

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
                    {drawerOpenData.dataList[drawerOpenData.index].Name}
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
                  key.charAt(0).toLowerCase() === key.charAt(0) || key === "Name" ? (
                    <React.Fragment key={key} />
                  ) : (
                    <Grid xs={6} item key={key}>
                      <Typography variant="subtitle2" className={classes.label}>
                        {key.split(/(?=[A-Z][^A-Z])/).join(" ")}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {value}
                      </Typography>
                    </Grid>
                  )
                )}
                {drawerOpenData.dataList[drawerOpenData.index].progress !== undefined && (
                  <Grid xs={12} item>
                    <Box display="flex" justifyContent="space-between">
                      <Typography variant="subtitle2" className={classes.label}>
                        {t("clusterDetail.activity.details.progress")}
                      </Typography>
                      {drawerOpenData.dataList[drawerOpenData.index].progress}%
                    </Box>
                    <Box mb={1}>
                      <YBProgress
                        value={drawerOpenData.dataList[drawerOpenData.index].progress}
                        color={theme.palette.primary[500]}
                      />
                    </Box>
                  </Grid>
                )}
              </Grid>
            </Box>
          </>
        )}
      </YBModal>
    </Box>
  );
};
