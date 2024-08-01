import React, { FC, useMemo } from "react";
import {
  Box,
  Divider,
  Grid,
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

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
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
  migration: Migration;
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
      uuid: migration.migration_uuid || "migration_uuid_not_found",
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

  const sourceObjects = useMemo(
    () =>
      sourceDBData?.sql_objects_count
        ?.filter((obj) => obj.sql_type)
        .map((obj) => ({
          type: obj.sql_type || "",
          count: obj.count || 0,
        })) || [],
    [sourceDBData]
  );

  const totalObjects = useMemo(() => {
    return sourceObjects.reduce((acc, obj) => acc + obj.count, 0);
  }, [sourceObjects]);

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
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "size",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.size"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (size: number) => getMemorySizeUnits(size),
      },
    },
    {
      name: "rowCount",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.rowCount"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "iops",
      label: t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.iops"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.heading")}
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
          <Box my={2}>
            <Paper>
              <Box p={2} className={classes.grayBg}>
                <Grid container spacing={4}>
                  <Grid item xs={2}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t(
                        "clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.totalObjects"
                      )}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {totalObjects}
                    </Typography>
                  </Grid>
                  <Grid item xs={1}>
                    <Divider orientation="vertical" />
                  </Grid>
                  {sourceObjects.map((obj) => (
                    <Grid item xs={2} key={obj.type}>
                      <Typography variant="subtitle2" className={classes.label}>
                        {obj.type}
                      </Typography>
                      <Typography variant="body2" className={classes.value}>
                        {obj.count}
                      </Typography>
                    </Grid>
                  ))}
                </Grid>
              </Box>
            </Paper>
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
                <MenuItem value="">All</MenuItem>
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
                placeholder={t(
                  "clusterDetail.voyager.planAndAssess.sourceEnv.sourceObjects.searchPlaceholder"
                )}
                InputProps={{
                  startAdornment: <SearchIcon />,
                }}
                onChange={(ev) => setSearch(ev.target.value)}
                value={search}
              />
            </Box>
          </Box>

          <Box>
            <YBTable
              data={filteredSourceObjects}
              columns={sourceObjectsColumns}
              options={{
                pagination: true,
              }}
            />
          </Box>
        </>
      )}
    </YBModal>
  );
};
