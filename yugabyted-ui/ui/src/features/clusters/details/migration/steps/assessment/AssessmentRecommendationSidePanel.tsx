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
import type { Migration } from "../../MigrationOverview";
import {
  AssessmentTargetRecommendationObject,
  useGetAssessmentTargetRecommendationInfoQuery,
} from "@app/api/src";
import { getMemorySizeUnits } from "@app/helpers";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    marginBottom: theme.spacing(0.75),
    padding: 0,
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

interface MigrationRecommendationSidePanel {
  open: boolean;
  onClose: () => void;
  migration: Migration | undefined;
}

export const MigrationRecommendationSidePanel: FC<MigrationRecommendationSidePanel> = ({
  open,
  onClose,
  migration,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: targetRecommendationAPI, isFetching: isFetchingTargetRecommendationData } =
    useGetAssessmentTargetRecommendationInfoQuery({
      uuid: migration?.migration_uuid || "migration_uuid_not_found",
    });

  const targetRecommendationData = targetRecommendationAPI as
    | AssessmentTargetRecommendationObject
    | undefined;

  const recommendationData = useMemo(
    () =>
      targetRecommendationData?.recommendation_details?.map((obj) => ({
        tableName: obj.table_name || "",
        diskSize: obj.disk_size || 0,
        schemaRecommendation: obj.schema_recommendation || "",
      })) ?? [],
    [targetRecommendationData]
  );

  const recommendationObjects = useMemo(
    () => ({
      colocated: {
        totalCount: targetRecommendationData?.num_of_colocated_tables || 0,
        totalSize: getMemorySizeUnits(targetRecommendationData?.total_size_colocated_tables || 0),
      },
      sharded: {
        totalCount: targetRecommendationData?.num_of_sharded_table,
        totalSize: getMemorySizeUnits(targetRecommendationData?.total_size_sharded_tables || 0),
      },
    }),
    [recommendationData]
  );

  const types = useMemo(() => {
    const typeSet = new Set<string>();
    recommendationData.forEach((obj) =>
      obj.schemaRecommendation ? typeSet.add(obj.schemaRecommendation) : null
    );
    return Array.from(typeSet);
  }, [recommendationData]);

  const [typeFilter, setTypeFilter] = React.useState<string>("");
  const [search, setSearch] = React.useState<string>("");

  const filteredSourceObjects = useMemo(() => {
    const searchQuery = search.toLowerCase().trim();
    return recommendationData.filter(
      (obj) =>
        (typeFilter === "" || obj.schemaRecommendation === typeFilter) &&
        (search === "" || obj.tableName.toLowerCase().includes(searchQuery))
    );
  }, [recommendationData, typeFilter, search]);

  const sourceObjectsColumns = [
    {
      name: "tableName",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schema.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "diskSize",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schema.diskSize"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (size: number) => getMemorySizeUnits(size),
      },
    },
    {
      name: "schemaRecommendation",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schema.schemaRecommendation"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.planAndAssess.recommendation.schema.heading")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      {isFetchingTargetRecommendationData && (
        <Box my={4}>
          <Box textAlign="center" mt={2.5}>
            <LinearProgress />
          </Box>
        </Box>
      )}

      {!isFetchingTargetRecommendationData && (
        <>
          <Box my={2}>
            <Paper>
              <Box p={2} className={classes.grayBg} display="flex" gridGap={20}>
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography variant="h5">
                      {t(
                        "clusterDetail.voyager.planAndAssess.recommendation.schema.colocatedTables"
                      )}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables")}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.colocated.totalCount}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.colocated.totalSize}
                    </Typography>
                  </Grid>
                </Grid>
                <Divider orientation="vertical" flexItem />
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography variant="h5">
                      {t("clusterDetail.voyager.planAndAssess.recommendation.schema.shardedTables")}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables")}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.sharded.totalCount}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.sharded.totalSize}
                    </Typography>
                  </Grid>
                </Grid>
              </Box>
            </Paper>
          </Box>

          <Box display="flex" alignItems="center" gridGap={10} my={2}>
            <Box flex={1}>
              <Typography variant="body1" className={classes.label}>
                {t(
                  "clusterDetail.voyager.planAndAssess.recommendation.schema.schemaRecommendation"
                )}
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
                {t("clusterDetail.voyager.planAndAssess.recommendation.schema.search")}
              </Typography>
              <YBInput
                className={classes.fullWidth}
                placeholder={t(
                  "clusterDetail.voyager.planAndAssess.recommendation.schema.searchPlaceholder"
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
