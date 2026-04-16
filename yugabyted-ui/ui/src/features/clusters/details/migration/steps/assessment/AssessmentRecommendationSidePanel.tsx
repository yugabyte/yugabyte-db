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
    color: '#6D7C88',
    fontWeight: 500,
    fontSize: '11.5px',
    padding: 0,
    textAlign: 'left',
    textTransform: 'uppercase',
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  dividerVertical: {
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(1),
    height: 'auto',
    alignSelf: 'stretch',
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
  headingCSS: {
    fontSize: '13px',
    fontWeight: 600,
  }
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
      uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
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
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schema.tableName"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "diskSize",
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schema.diskSize"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (size: number) => getMemorySizeUnits(size),
      },
    },
    {
      name: "schemaRecommendation",
      label: t(
        "clusterDetail.voyager.planAndAssess.recommendation.schema.schemaRecommendation"
      ),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  // Add pagination state for the recommendation objects table
  const [recommendationTablePage, setRecommendationTablePage] = React.useState(0);
  const [recommendationTableRowsPerPage, setRecommendationTableRowsPerPage] = React.useState(10);

  return (
    <YBModal
      open={open}
      title={t(
        "clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.heading"
      )}
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
            <Paper
              style={{ border: '1px solid #E9EEF2' }}
            >
              <Box p={2}
                className={classes.grayBg}
                alignItems="stretch"
                display="flex"
                gridGap={16}
              >
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography variant="h5" className={classes.headingCSS}>
                      {t(
                        "clusterDetail.voyager.planAndAssess.recommendation.schema.colocatedTables"
                      )}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t(
                        "clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables"
                      )}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.colocated.totalCount}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t(
                        "clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize"
                      )}
                    </Typography>
                    <Typography variant="body2" className={classes.value}>
                      {recommendationObjects.colocated.totalSize}
                    </Typography>
                  </Grid>
                </Grid>
                <Divider orientation="vertical" flexItem className={classes.dividerVertical} />
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography variant="h5" className={classes.headingCSS}>
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
                page: recommendationTablePage,
                rowsPerPage: recommendationTableRowsPerPage,
                onChangePage: (currentPage: number) => setRecommendationTablePage(currentPage),
                onChangeRowsPerPage: (numberOfRows: number) =>
                  setRecommendationTableRowsPerPage(numberOfRows)
              }}
            />
          </Box>
        </>
      )}
    </YBModal>
  );
};
