import React, { FC, useMemo } from "react";
import { makeStyles, Box, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import { YBButton, YBInput, YBLoadingBox, YBTable } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import SearchIcon from "@app/assets/search.svg";

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
  arrowComponent: {
    textAlign: "end",
    "& svg": {
      marginTop: theme.spacing(0.25),
    },
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

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
};

export const BackupList: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [backupSearch, setBackupSearch] = React.useState<string>("");

  const data = [
    {
      ybc_task_id: "036fd4fb-c84b-4b39-94da-8417e7dafe1f",
      tserver_ip: "127.0.0.1",
      user_operation: "backup",
      ybdb_api: "ysql",
      database_keyspace: "yugabyte",
      task_start_time: "March 21 2024 - 14:00:30",
      task_status: "OK",
      time_taken: "37977 ms",
      bytes_transferred: "0.04 MB",
      actual_size: "0.04 MB",
    },
  ];

  const filteredData = useMemo(
    () =>
      data.filter((item) => {
        return item.database_keyspace.toLowerCase().includes(backupSearch.toLowerCase());
      }),
    [backupSearch, data]
  );

  const columns = [
    {
      name: "database_keyspace",
      label: t("clusterDetail.databases.backups.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "tserver_ip",
      label: t("clusterDetail.databases.backups.tserverIP"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "bytes_transferred",
      label: t("clusterDetail.databases.backups.bytesTransferred"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "actual_size",
      label: t("clusterDetail.databases.backups.actualSize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "task_status",
      label: t("clusterDetail.databases.backups.taskStatus"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "time_taken",
      label: t("clusterDetail.databases.backups.timeTaken"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "task_start_time",
      label: t("clusterDetail.databases.backups.taskStartTime"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    /* {
      name: "",
      label: "",
      options: {
        sort: false,
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: ArrowComponent(classes),
      },
    }, */
  ];

  const onRefetch = () => {};

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Typography variant="subtitle2" className={classes.label}>
          {t("clusterDetail.databases.backups.backups")}
        </Typography>
        <Typography variant="body2" className={classes.value}>
          {data.length}
        </Typography>
      </Box>

      <Box display="flex" alignItems="center" justifyContent="end" my={2}>
        <YBInput
          placeholder={t("clusterDetail.databases.backups.searchBackups")}
          InputProps={{
            startAdornment: <SearchIcon />,
          }}
          className={classes.searchBox}
          onChange={(ev) => setBackupSearch(ev.target.value)}
          value={backupSearch}
        />
        <YBButton
          variant="ghost"
          startIcon={<RefreshIcon />}
          className={classes.refreshBtn}
          onClick={onRefetch}
        >
          {t("clusterDetail.databases.backups.refresh")}
        </YBButton>
      </Box>
      {!data.length ? (
        <YBLoadingBox>{t("clusterDetail.databases.backups.noBackups")}</YBLoadingBox>
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
