import React, { FC, useMemo } from "react";
import { makeStyles, Box, Typography, MenuItem, Paper } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { getMemorySizeUnits } from "@app/helpers";
import ArrowRightIcon from "@app/assets/caret-right-circle.svg";
import {
  YBButton,
  YBCheckbox,
  YBDropdown,
  YBInput,
  YBLoadingBox,
  YBSelect,
  YBTable,
} from "@app/components";
import { useGetClusterHealthCheckQuery, useGetClusterTabletsQuery } from "@app/api/src";
import SearchIcon from "@app/assets/search.svg";
import RefreshIcon from "@app/assets/refresh.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { TableListType } from "./TablesTab";

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
    display: "flex",
    gap: theme.spacing(4),
    marginTop: theme.spacing(3),
    marginBottom: theme.spacing(4),
  },
  leftBorder: {
    paddingStart: `${theme.spacing(2)}px`,
    borderWidth: "0 0 0 1px",
    borderStyle: "solid",
    borderColor: theme.palette.grey[300],
  },
  dropdown: {
    width: 240,
    marginRight: theme.spacing(1),
  },
  searchBox: {
    maxWidth: 520,
    flexGrow: 1,
    marginRight: "auto",
  },
  refreshBtn: {
    marginRight: theme.spacing(1),
  },
  checkbox: {
    padding: theme.spacing(0.5, 0.5, 0.5, 1.5),
  },
  statusContainer: {
    display: "flex",
    flexDirection: "column",
    width: 240,
  },
  paperContainer: {
    padding: theme.spacing(3),
    marginBottom: theme.spacing(3),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: "100%",
  },
}));

type DatabaseListProps = {
  tableList: TableListType;
  onRefetch: () => void;
  onSelect: (name: string, uuid: string) => void;
};

const ArrowComponent = (classes: ReturnType<typeof useStyles>) => () => {
  return (
    <Box className={classes.arrowComponent}>
      <ArrowRightIcon />
    </Box>
  );
};

export const TableList: FC<DatabaseListProps> = ({
  tableList: tableListProp,
  onSelect,
  onRefetch,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: tablets } = useGetClusterTabletsQuery();
  const { data: healthCheckData } = useGetClusterHealthCheckQuery();

  const tabletStatusList = useMemo(
    () =>
      [
        { key: t("clusterDetail.databases.healthyReplication"), value: "Healthy" },
        { key: t("clusterDetail.databases.underReplicated"), value: "Under-replicated" },
        { key: t("clusterDetail.databases.unavailable"), value: "Unavailable" },
      ] as const,
    [t]
  );

  const [tabletStatus, setTabletStatus] = React.useState<
    (typeof tabletStatusList)[number]["value"][]
  >(tabletStatusList.map((t) => t.value));
  const [tableName, setTableName] = React.useState<string>("");

  const handleStatusChange = (newStatus: (typeof tabletStatus)[number]) => {
    const statusIndex = tabletStatus.findIndex((s) => s === newStatus);
    const newTabletStatus = [...tabletStatus];
    if (statusIndex !== -1) {
      newTabletStatus.splice(statusIndex, 1);
    } else {
      newTabletStatus.push(newStatus);
    }
    setTabletStatus(newTabletStatus);
  };

  const tableList = useMemo(() => {
    let tabletList = tablets ? Object.entries(tablets.data).map(([_, value]) => value) : [];
    return tableListProp.map((table) => ({
      ...table,
      status: tabletList.some(
        (tablet) =>
          tablet.tablet_id &&
          tablet.table_uuid === table.uuid &&
          healthCheckData?.data?.under_replicated_tablets?.includes(tablet.tablet_id)
      )
        ? "Under-replicated"
        : tabletList.some(
            (tablet) =>
              tablet.tablet_id &&
              tablet.table_uuid === table.uuid &&
              healthCheckData?.data?.leaderless_tablets?.includes(tablet.tablet_id)
          )
        ? "Unavailable"
        : "Healthy",
    }));
  }, [healthCheckData, tablets, tableListProp]);

  const [clusterTables, indexTables] = useMemo(() => {
    let data = tableList;
    if (tabletStatus.length !== 0) {
      data = data.filter((data) => tabletStatus.find((status) => status === data.status));
    } else {
      data = [];
    }
    if (tableName) {
      const searchName = tableName.toLowerCase();
      data = data.filter((data) => data.name.toLowerCase().includes(searchName));
    }

    return [
      data.filter((table) => !table.isIndexTable),
      data.filter((table) => !!table.isIndexTable),
    ];
  }, [tableList, tabletStatus, tableName]);

  const indexTableExists = useMemo(
    () => !!tableList.find((table) => !!table.isIndexTable),
    [tableList]
  );

  const columns = [
    {
      name: "name",
      label: t("clusterDetail.databases.table"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "keyspace",
      label: t("clusterDetail.databases.partition"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "size_bytes",
      label: t("clusterDetail.databases.size"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (value: number) => getMemorySizeUnits(value),
      },
    },
    {
      name: "status",
      label: "",
      options: {
        sort: false,
        hideHeader: true,
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (status: string) =>
          status !== "Healthy" && (
            <YBBadge
              variant={status === "Under-replicated" ? BadgeVariant.Warning : BadgeVariant.Error}
              text={
                status === "Under-replicated"
                  ? t("clusterDetail.databases.underReplicated")
                  : t("clusterDetail.databases.unavailable")
              }
            />
          ),
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

  return (
    <Box>
      <Box className={classes.statContainer}>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t("clusterDetail.databases.totalTables")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {tableList.length}
          </Typography>
        </Box>
        <Box>
          <Typography variant="subtitle2" className={classes.label}>
            {t("clusterDetail.databases.totalSize")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {getMemorySizeUnits(tableList.reduce((prev, curr) => prev + curr.size_bytes, 0))}
          </Typography>
        </Box>
        <Box className={classes.leftBorder}>
          <Typography variant="subtitle2" className={classes.label}>
            {t("clusterDetail.databases.underReplicatedTables")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {tableList.filter((table) => table.status === "Under-replicated").length}
          </Typography>
        </Box>
      </Box>
      <Typography variant="subtitle2" className={classes.label} style={{ marginBottom: 0 }}>
        {t("clusterDetail.databases.tabletStatus")}
      </Typography>
      <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
        {/* <YBSelect
          value={tabletStatus}
          onChange={(ev) => setTabletStatus(ev.target.value)}
          className={classes.dropdown}
        >
          <MenuItem value={''}>{t('clusterDetail.databases.allStatuses')}</MenuItem>
          {tabletStatusList.map(tableStatus => 
            <MenuItem key={tableStatus.key} value={tableStatus.value}>{tableStatus.key}</MenuItem>
          )}
        </YBSelect> */}
        <YBDropdown
          origin={
            <YBSelect
              inputProps={{ tabIndex: -1 }}
              style={{ pointerEvents: "none" }}
              className={classes.dropdown}
              value={tabletStatus.join(", ")}
            >
              <MenuItem value={tabletStatus.join(", ")}>
                {tabletStatus.length === 0
                  ? t("clusterDetail.databases.none")
                  : tabletStatus.length < 3
                  ? tabletStatus.join(", ")
                  : t("clusterDetail.databases.allStatuses")}
              </MenuItem>
            </YBSelect>
          }
          position={"bottom"}
          growDirection={"right"}
          keepOpenOnSelect
        >
          <Box className={classes.statusContainer}>
            {tabletStatusList.map((tableStatus) => (
              <YBCheckbox
                key={tableStatus.key}
                label={tableStatus.key}
                className={classes.checkbox}
                checked={!!tabletStatus.find((status) => status === tableStatus.value)}
                onClick={() => handleStatusChange(tableStatus.value)}
              />
            ))}
          </Box>
        </YBDropdown>
        <YBInput
          placeholder={t("clusterDetail.databases.searchTableName")}
          InputProps={{
            startAdornment: <SearchIcon />,
          }}
          className={classes.searchBox}
          onChange={(ev) => setTableName(ev.target.value)}
          value={tableName}
        />
        <YBButton
          variant="ghost"
          startIcon={<RefreshIcon />}
          className={classes.refreshBtn}
          onClick={onRefetch}
        >
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      <Paper className={classes.paperContainer}>
        <Box mb={3}>
          <Typography variant="h4">{t("clusterDetail.tables.clusterTables")}</Typography>
        </Box>
        {!clusterTables.length ? (
          <YBLoadingBox>{t("clusterDetail.tables.noClusterTables")}</YBLoadingBox>
        ) : (
          <YBTable
            data={clusterTables}
            columns={columns}
            options={{
              pagination: true,
              rowHover: true,
              onRowClick: (_, { dataIndex }) =>
                onSelect(clusterTables[dataIndex].name, clusterTables[dataIndex].uuid),
            }}
            withBorder={false}
          />
        )}
      </Paper>

      {indexTableExists && (
        <Paper className={classes.paperContainer}>
          <Box mb={3}>
            <Typography variant="h4">{t("clusterDetail.tables.indexTables")}</Typography>
          </Box>
          {!indexTables.length ? (
            <YBLoadingBox>{t("clusterDetail.tables.noIndexTables")}</YBLoadingBox>
          ) : (
            <YBTable
              data={indexTables}
              columns={columns}
              options={{
                pagination: true,
                rowHover: true,
                onRowClick: (_, { dataIndex }) =>
                  onSelect(indexTables[dataIndex].name, indexTables[dataIndex].uuid),
              }}
              withBorder={false}
            />
          )}
        </Paper>
      )}
    </Box>
  );
};
