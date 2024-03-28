import React, { FC, useMemo } from "react";
import { makeStyles, Box, Typography, LinearProgress } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBInput, YBLoadingBox, YBTable } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import SearchIcon from "@app/assets/search.svg";
import { useGetPITRSchedulesQuery } from "@app/api/src";

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

export const PitrList: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [search, setSearch] = React.useState<string>("");

  const { data, isFetching, refetch } = useGetPITRSchedulesQuery();

  const pitrData = data?.schedules ?? [];

  const filteredData = useMemo(
    () =>
      pitrData.filter((item) => {
        return item.databaseKeyspace.toLowerCase().includes(search.toLowerCase());
      }),
    [search, data]
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
          {t("clusterDetail.databases.pitr.pitr")}
        </Typography>
        <Typography variant="body2" className={classes.value}>
          {pitrData.length}
        </Typography>
      </Box>
      <Box display="flex" alignItems="center" justifyContent="end" my={2}>
        <YBInput
          placeholder={t("clusterDetail.databases.pitr.searchPitr")}
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
          {t("clusterDetail.databases.pitr.refresh")}
        </YBButton>
      </Box>
      {!pitrData.length ? (
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
