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
      database: "yugabyted",
      apiType: "YSQL",
      size: "213 MB",
      progress: "100%",
      startedAt: "2021-09-01T00:00:00.000Z",
    },
    {
      database: "customer",
      apiType: "YCQL",
      size: "75 MB",
      progress: "70%",
      startedAt: "2021-10-01T00:00:00.000Z",
    },
  ];

  const filteredData = useMemo(
    () =>
      data.filter((item) => {
        return item.database.toLowerCase().includes(backupSearch.toLowerCase());
      }),
    [backupSearch, data]
  );

  const columns = [
    {
      name: "database",
      label: t("clusterDetail.databases.backups.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "apiType",
      label: t("clusterDetail.databases.backups.apiType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "size",
      label: t("clusterDetail.databases.backups.size"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "progress",
      label: t("clusterDetail.databases.backups.progress"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "startedAt",
      label: t("clusterDetail.databases.backups.backupStarted"),
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
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: ArrowComponent(classes),
      },
    },
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
            rowHover: true,
          }}
          touchBorder={false}
        />
      )}
    </Box>
  );
};
