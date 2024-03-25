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

export const PitrList: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [backupSearch, setBackupSearch] = React.useState<string>("");

  const data = [
    {
      id: 1,
      databaseKeyspace: "yugabyte",
      interval: "24 hours",
      retention: "7 days",
      earliestRecoverableTime: "2024-03-21 03:45:22 PM",
    },
  ];

  const filteredData = useMemo(
    () =>
      data.filter((item) => {
        return item.databaseKeyspace.toLowerCase().includes(backupSearch.toLowerCase());
      }),
    [backupSearch, data]
  );

  const columns = [
    {
      name: "databaseKeyspace",
      label: t("clusterDetail.databases.pitr.database"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "interval",
      label: t("clusterDetail.databases.pitr.interval"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "retention",
      label: t("clusterDetail.databases.pitr.retention"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "earliestRecoverableTime",
      label: t("clusterDetail.databases.pitr.recoverableTime"),
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
          {t("clusterDetail.databases.pitr.pitr")}
        </Typography>
        <Typography variant="body2" className={classes.value}>
          {data.length}
        </Typography>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="end" my={2}>
        <YBInput
          placeholder={t("clusterDetail.databases.pitr.searchPitr")}
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
          {t("clusterDetail.databases.pitr.refresh")}
        </YBButton>
      </Box>
      {!data.length ? (
        <YBLoadingBox>{t("clusterDetail.databases.pitr.noPitr")}</YBLoadingBox>
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
