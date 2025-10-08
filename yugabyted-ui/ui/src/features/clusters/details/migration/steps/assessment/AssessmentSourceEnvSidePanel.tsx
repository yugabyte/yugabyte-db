import React, { FC, useMemo } from "react";
import {
  Box,
  Divider,
  LinearProgress,
  MenuItem,
  Paper,
  Typography,
  makeStyles,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBInput, YBModal, YBSelect, YBTable } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import { AssessmentSourceDbObject, useGetAssessmentSourceDBInfoQuery } from "@app/api/src";
import type { Migration } from "../../MigrationOverview";
import { getMemorySizeUnits } from "@app/helpers";
import { MetadataItem } from "../../components/MetadataItem";
import { YBMultiToggleButton, ToggleButtonData } from "@app/components/YBMultiToggleButton";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: "#6D7C88",
    fontWeight: 500,
    textTransform: "uppercase",
    fontSize: "11.5px",
    textAlign: "left",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
  grayBg: {
    backgroundColor: theme.palette.background.default,
    borderRadius: theme.shape.borderRadius,
  },
  fullWidth: {
    width: "100%",
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
}));

interface MigrationSourceEnvSidePanelProps {
  open: boolean;
  onClose: () => void;
  migration: Migration | undefined;
}

export const MigrationSourceEnvSidePanel: FC<MigrationSourceEnvSidePanelProps> = ({
  open,
  onClose,
  migration,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: sourceDBDataAPI, isFetching: isFetchingSourceDBData } =
    useGetAssessmentSourceDBInfoQuery({
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
    });

  const sourceDBData = sourceDBDataAPI as AssessmentSourceDbObject | undefined;

  const sourceObjectData = useMemo(() => {
    if (!sourceDBData?.sql_objects_metadata) {
      return [];
    }

    return sourceDBData.sql_objects_metadata.map((obj) => ({
      name: obj.object_name || "",
      type: obj.sql_type || "",
      size: obj.size || 0,
      rowCount: obj.row_count || 0,
      iops: obj.iops || 0,
    }));
  }, [sourceDBData]);

  // const sourceObjects = useMemo(
  //   () =>
  //     sourceDBData?.sql_objects_count
  //       ?.filter((obj) => obj.sql_type)
  //       .map((obj) => ({
  //         type: obj.sql_type || "",
  //         count: obj.count || 0,
  //       })) || [],
  //   [sourceDBData]
  // );

  const types = useMemo(() => {
    const typeSet = new Set<string>();
    sourceObjectData.forEach((obj) => (obj.type ? typeSet.add(obj.type) : null));
    return Array.from(typeSet);
  }, [sourceObjectData]);

  const [typeFilter, setTypeFilter] = React.useState<string>("");
  const [search, setSearch] = React.useState<string>("");

  const filteredSourceObjects = useMemo(() => {
    const searchQuery = search.toLowerCase().trim();
    return sourceObjectData.filter(
      (obj) =>
        (typeFilter === "" || obj.type === typeFilter) &&
        (search === "" || obj.name.toLowerCase().includes(searchQuery))
    );
  }, [sourceObjectData, typeFilter, search]);

  // Calculate total size for filtered objects
  const filteredTotalSize = useMemo(() => {
    return filteredSourceObjects
      .filter(obj => obj.size !== -1)
      .reduce((acc, obj) => acc + obj.size, 0);
  }, [filteredSourceObjects]);

  const groupedObjectData = useMemo(() => {
    const grouped = sourceObjectData.reduce((acc, obj) => {
      const type = obj.type || 'Unknown';
      if (!acc[type]) {
        acc[type] = {
          type,
          count: 0,
          totalSize: 0,
          totalIops: 0,
          objects: []
        };
      }
      acc[type].count++;
      if (obj.size !== -1)
        acc[type].totalSize += obj.size;
      if (obj.iops !== -1)
        acc[type].totalIops += obj.iops;
      acc[type].objects.push(obj);
      return acc;
    }, {} as Record<string, {
      type: string;
      count: number;
      totalSize: number;
      totalIops: number;
      objects: any[];
    }>);

    const searchQuery = search.toLowerCase().trim();
    return Object.values(grouped).filter(group =>
      (typeFilter === "" || group.type === typeFilter) &&
      (search === "" || group.type.toLowerCase().includes(searchQuery))
    );
  }, [sourceObjectData, typeFilter, search]);

  const groupedColumns = [
    {
      name: "type",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.type"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", textTransform: "capitalize" } }),
      },
    },
    {
      name: "count",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.count"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "totalSize",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.totalSize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (size: number) => (
          size === 0 ? <Typography component="span">-</Typography> : getMemorySizeUnits(size)
        ),
      },
    },
    // {
    //   name: "totalIops",
    //   label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.totalIops"),
    //   options: {
    //     setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
    //     setCellProps: () => ({ style: { padding: "8px 16px" } }),
    //     customBodyRender: (iops: number) => (
    //       iops === 0 ? <Typography component="span">-</Typography> : iops
    //     ),
    //   },
    // },
  ];

  const sourceObjectsColumns = [
    {
      name: "name",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.objectName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "type",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.type"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", textTransform: "capitalize" } }),
      },
    },
    {
      name: "size",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.size"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (size: number) => (
          size === -1 ? <span>-</span> : getMemorySizeUnits(size)
        ),

      },
    },
    /* {
      name: "rowCount",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.rowCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    }, */
    {
      name: "iops",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.iops"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (iops: number) => (
          iops === -1 ? <Typography component="span">-</Typography> : iops
        ),
      },
    },
  ];

  const [sourceObjectsTablePage, setSourceObjectsTablePage] = React.useState(0);
  const [sourceObjectsTableRowsPerPage, setSourceObjectsTableRowsPerPage] = React.useState(10);

  const [toggleValue, setToggleValue] = React.useState<string>("all");
  const toggleOptions: ToggleButtonData<string>[] = [
    { label: t("clusterDetail.voyager.planAndAssess.sourceEnv.groupByObjectType"), value: "group" },
    { label: t("clusterDetail.voyager.planAndAssess.sourceEnv.listAllObjects"), value: "all" }
  ];

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.planAndAssess.sourceEnv.schemaDetails")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      {isFetchingSourceDBData && (
        <Box my={4}>
          <Box textAlign="center" mt={2.5}>
            <LinearProgress />
          </Box>
        </Box>
      )}

      {!isFetchingSourceDBData && (
        <>
          <Box display="flex" justifyContent="flex-end" alignItems="center" mb={2}>
            <YBMultiToggleButton
              options={toggleOptions}
              value={toggleValue}
              onChange={setToggleValue}
            />
          </Box>
          <Box display="flex" alignItems="center" gridGap={10} my={2}>
            <Box flex={1}>
              <Typography variant="body1" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.type")}
              </Typography>
              <YBSelect
                className={classes.fullWidth}
                value={typeFilter}
                onChange={(e) => setTypeFilter(e.target.value)}
              >
                <MenuItem value="" className={classes.label}>All</MenuItem>
                <Divider className={classes.divider} />
                {types.map((type) => {
                  return (
                    <MenuItem key={type} value={type}>
                      {type}
                    </MenuItem>
                  );
                })}
              </YBSelect>
            </Box>
            <Box flex={2}>
              <Typography variant="body1" className={classes.label}>
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.search")}
              </Typography>
              <YBInput
                className={classes.fullWidth}
                placeholder={
                  toggleValue === "all"
                    ? t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects."
                      + "searchPlaceholder")
                    : "Search object type"
                }
                InputProps={{
                  startAdornment: <SearchIcon />,
                }}
                onChange={(ev) => setSearch(ev.target.value)}
                value={search}
              />
            </Box>

          </Box>
          <Box my={2}>
            <Paper
              style={{ border: '1px solid #E9EEF2' }}
            >
              <Box p={2} className={classes.grayBg}>
                <Box display="flex" flexDirection="row">
                  <MetadataItem
                    layout="vertical"
                    label={
                      t('clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.totalObjects')
                    }
                    value={toggleValue === "all"
                      ? filteredSourceObjects.length
                      : groupedObjectData.reduce((acc, group) => acc + group.count, 0)
                    }
                  />
                  <MetadataItem
                    layout="vertical"
                    label={
                      t('clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.totalSize')
                    }
                    value={toggleValue === "all"
                      ? getMemorySizeUnits(filteredTotalSize)
                      : getMemorySizeUnits(groupedObjectData.reduce((acc, group) =>
                        acc + group.totalSize, 0))
                    }
                  />
                  {toggleValue === "group" && (
                    <MetadataItem
                      layout="vertical"
                      label={t('Object Types')}
                      value={groupedObjectData.length}
                    />
                  )}
                </Box>
              </Box>
            </Paper>
          </Box>

          <Box>
            {toggleValue === "all" ? (
              <YBTable
                data={filteredSourceObjects}
                columns={sourceObjectsColumns}
                options={{
                  pagination: true,
                  page: sourceObjectsTablePage,
                  rowsPerPage: sourceObjectsTableRowsPerPage,
                  onChangePage: (currentPage: number) => setSourceObjectsTablePage(currentPage),
                  onChangeRowsPerPage: (numberOfRows: number) =>
                    setSourceObjectsTableRowsPerPage(numberOfRows)
                }}
              />
            ) : (
              <YBTable
                data={groupedObjectData}
                columns={groupedColumns}
                options={{
                  pagination: true,
                  page: sourceObjectsTablePage,
                  rowsPerPage: sourceObjectsTableRowsPerPage,
                  onChangePage: (currentPage: number) => setSourceObjectsTablePage(currentPage),
                  onChangeRowsPerPage: (numberOfRows: number) =>
                    setSourceObjectsTableRowsPerPage(numberOfRows)
                }}
              />
            )}
          </Box>
        </>
      )}
    </YBModal>
  );
};
