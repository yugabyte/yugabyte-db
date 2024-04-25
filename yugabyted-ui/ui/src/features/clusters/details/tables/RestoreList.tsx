import React, { FC, useMemo } from "react";
import { makeStyles, Box, Typography, LinearProgress } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBInput, YBLoadingBox, YBTable } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import SearchIcon from "@app/assets/search.svg";
import { useGetRestoreDetailsQuery } from "@app/api/src";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    fontWeight: theme.typography.fontWeightMedium as number,
    color: theme.palette.grey[800],
    fontSize: "18px",
    textAlign: "start",
  },
  statContainer: {
    marginTop: theme.spacing(4),
  },
  refreshBtn: {
    marginRight: theme.spacing(1),
  },
  searchBox: {
    maxWidth: 320,
    flexGrow: 1,
    marginRight: "auto",
  },
}));

export const RestoreList: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [search, setSearch] = React.useState<string>("");

  const { data, isFetching, refetch } = useGetRestoreDetailsQuery();

  const restoreData = data?.restore ?? [];

  const filteredData = useMemo(
    () =>
      restoreData.filter((item) => {
        return item.database_keyspace.toLowerCase().includes(search.toLowerCase());
      }),
    [search, data]
  );

  const columns = [
    {
      name: "database_keyspace",
      label: t("clusterDetail.databases.restore.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "tserver_ip",
      label: t("clusterDetail.databases.restore.tserverIP"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "bytes_transferred",
      label: t("clusterDetail.databases.restore.bytesTransferred"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "actual_size",
      label: t("clusterDetail.databases.restore.actualSize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "task_status",
      label: t("clusterDetail.databases.restore.taskStatus"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "time_taken",
      label: t("clusterDetail.databases.restore.timeTaken"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "task_start_time",
      label: t("clusterDetail.databases.restore.taskStartTime"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  const onRefetch = () => {
    refetch();
  };

  if (isFetching) {
    return (
      <Box my={4}>
        <Box textAlign="center" mt={2.5}>
          <LinearProgress />
        </Box>
      </Box>
    );
  }

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Typography variant="subtitle2" className={classes.label}>
          {t("clusterDetail.databases.restore.restores")}
        </Typography>
        <Typography variant="body2" className={classes.value}>
          {restoreData.length}
        </Typography>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="end" my={2}>
        <YBInput
          placeholder={t("clusterDetail.databases.restore.searchRestores")}
          InputProps={{
            startAdornment: <SearchIcon />,
          }}
          className={classes.searchBox}
          onChange={(ev) => setSearch(ev.target.value)}
          value={search}
        />
        <YBButton
          variant="ghost"
          startIcon={<RefreshIcon />}
          className={classes.refreshBtn}
          onClick={onRefetch}
        >
          {t("clusterDetail.databases.restore.refresh")}
        </YBButton>
      </Box>
      {!restoreData.length ? (
        <YBLoadingBox>{t("clusterDetail.databases.restore.noRestores")}</YBLoadingBox>
      ) : (
        <YBTable
          data={filteredData}
          columns={columns}
          options={{
            pagination: false,
          }}
          touchBorder={false}
        />
      )}
    </Box>
  );
};
