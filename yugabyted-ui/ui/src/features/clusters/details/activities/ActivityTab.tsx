import React, { FC } from "react";
import { Box, Grid, makeStyles, MenuItem, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTable, YBLoadingBox, YBModal, YBSelect, YBButton } from "@app/components";
import { YBBadge } from "@app/components/YBBadge/YBBadge";
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
  );;

  const [selectedDB, setSelectedDB] = React.useState<string>(ysqlDBList[0] ?? "");
  React.useEffect(() => {
    if (
      (ysqlDBList.length > 0 && selectedDB === "") ||
      !ysqlDBList.find((db) => db === selectedDB)
    ) {
      setSelectedDB(ysqlDBList[0] ?? "");
    }
  }, [ysqlDBList]);

  const { data: activityData, refetch: refetchActivities } = useActivities();
  const [drawerOpenIndex, setDrawerOpenIndex] = React.useState<number>();

  const refetch = React.useCallback(() => {
    refetchActivities();
    refetchYsql();
  }, [refetchActivities, refetchYsql]);

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
        customBodyRenderLite: (dataIndex: number) => {
          const activity = activityData[dataIndex];
          return <YBBadge variant={activity.status} text={activity.Phase} />;
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
    if (drawerOpenIndex === undefined) {
      return undefined;
    }

    return Object.entries<any>(activityData[drawerOpenIndex]);
  }, [drawerOpenIndex, activityData]);

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
      {activityData.length ? (
        <Box pb={5}>
          <YBTable
            data={activityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) => setDrawerOpenIndex(dataIndex),
            }}
            touchBorder={false}
          />
          <YBModal
            open={drawerOpenIndex !== undefined}
            title={t("clusterDetail.activity.details.title")}
            onClose={() => setDrawerOpenIndex(undefined)}
            enableBackdropDismiss
            titleSeparator
            cancelLabel={t("common.close")}
            isSidePanel
          >
            {drawerOpenIndex !== undefined && (
              <>
                <Box className={classes.activityDetailBox}>
                  <Grid container spacing={2}>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t("clusterDetail.activity.details.operationName")}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {activityData[drawerOpenIndex].name}
                      </Typography>
                    </Grid>
                    <Grid xs={6} item>
                      <Typography variant="subtitle2" className={classes.label}>
                        {t("clusterDetail.activity.details.status")}
                      </Typography>
                      <YBBadge
                        variant={activityData[drawerOpenIndex].status}
                        text={activityData[drawerOpenIndex].Phase}
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
      ) : (
        <YBLoadingBox>{t("clusterDetail.activity.noInprogressActivities")}</YBLoadingBox>
      )}

      <Box className={classes.sectionHeading}>
        {t("clusterDetail.activity.completedActivities")}
      </Box>
      {activityData.length ? (
        <Box pb={4}>
          <YBTable
            data={activityData}
            columns={activityColumns}
            options={{
              pagination: false,
              rowHover: true,
              onRowClick: (_, { dataIndex }) => setDrawerOpenIndex(dataIndex),
            }}
            touchBorder={false}
          />
        </Box>
      ) : (
        <YBLoadingBox>{t("clusterDetail.activity.noCompletedActivities")}</YBLoadingBox>
      )}
    </Box>
  );
};
