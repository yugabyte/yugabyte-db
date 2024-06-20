import React, { FC } from "react";
import {
  Box,
  Grid,
  LinearProgress,
  Link,
  makeStyles,
  Typography,
  useTheme,
} from "@material-ui/core";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, YBButton } from "@app/components";
import { useTranslation } from "react-i18next";
import RefreshIcon from "@app/assets/refresh.svg";
import clsx from "clsx";
import { Prereqs } from "./Prereqs";
import { StepDetails } from "./StepDetails";
import { StepCard } from "./StepCard";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
  stat: {
    display: "flex",
    gap: theme.spacing(6),
    paddingRight: theme.spacing(6),
    marginRight: theme.spacing(2),
    borderRight: `1px solid ${theme.palette.grey[300]}`,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
  },
  statLabel: {
    marginBottom: theme.spacing(0.75),
  },
  value: {
    color: theme.palette.grey[700],
    paddingTop: theme.spacing(0.57),
    textAlign: "left",
  },
}));

const exportSchemaPrereqs: React.ReactNode[] = [
  <>
    Ensure{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#prepare-the-source-database"
      target="_blank"
    >
      the source and target database are prepared
    </Link>{" "}
    for migration.
  </>,
  <>
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#create-an-export-directory"
      target="_blank"
    >
      Create an export directory
    </Link>{" "}
    on the Voyager instance to track the migration state including exported schema and data.
  </>,
];

const importSchemaPrereqs: React.ReactNode[] = [
  <>
    Make sure your cluster size matches the recommendation in{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"
      target="_blank"
    >
      Assessment
    </Link>
    .
  </>,
  <>
    Modify tables and schema according to the DDL recommendations in{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"
      target="_blank"
    >
      Assessment
    </Link>
    .
  </>,
];

interface MigrationSchemaProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationSchema: FC<MigrationSchemaProps> = ({
  heading,
  /* migration, */
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const isErrorMigrationSchemaTasks = false;
  const isFetchingAPI = false;

  return (
    <Box>
      <Box display="flex" justifyContent="space-between" alignItems="start">
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      {isErrorMigrationSchemaTasks && <GenericFailure />}

      {(isFetching || isFetchingAPI) && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationSchemaTasks) && (
        <>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <StepCard title={t("clusterDetail.voyager.migrateSchema.schemaExportSourceDB")}>
              {(isDone) =>
                !isDone ? (
                  <>
                    <Prereqs items={exportSchemaPrereqs} />
                    <StepDetails
                      heading={t("clusterDetail.voyager.migrateSchema.schemaExport")}
                      message={t("clusterDetail.voyager.migrateSchema.schemaExportDesc")}
                      docsText={t("clusterDetail.voyager.migrateSchema.schemaExportLearn")}
                      docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#export-schema"
                    />
                  </>
                ) : null
              }
            </StepCard>
            <StepCard title={t("clusterDetail.voyager.migrateSchema.schemaAnalysis")} showTooltip>
              {(isDone) =>
                !isDone ? (
                  <>
                    <StepDetails
                      heading={t("clusterDetail.voyager.migrateSchema.schemaAnalysis")}
                      message={t("clusterDetail.voyager.migrateSchema.schemaAnalysisDesc")}
                      docsText={t("clusterDetail.voyager.migrateSchema.schemaAnalysisLearn")}
                      docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#analyze-schema"
                    />
                  </>
                ) : (
                  <Grid container>
                    <div>
                      <div className={classes.stat}>
                        <div>
                          <Typography
                            variant="body1"
                            className={clsx(classes.label, classes.statLabel)}
                          >
                            {t("clusterDetail.voyager.migrateSchema.automaticDDLImport")}
                          </Typography>
                          <Typography variant="h4" className={classes.value}>
                            113
                          </Typography>
                        </div>

                        <div>
                          <Typography
                            variant="body1"
                            className={clsx(classes.label, classes.statLabel)}
                          >
                            {t("clusterDetail.voyager.migrateSchema.manualRefactoring")}
                          </Typography>
                          <Typography variant="h4" className={classes.value}>
                            32
                          </Typography>
                        </div>
                      </div>
                    </div>
                    <div>
                      <Typography
                        variant="body1"
                        className={clsx(classes.label, classes.statLabel)}
                      >
                        {t("clusterDetail.voyager.migrateSchema.totalAnalyzed")}
                      </Typography>
                      <Typography variant="h4" className={classes.value}>
                        {113 + 32}
                      </Typography>
                    </div>
                  </Grid>
                )
              }
            </StepCard>
            <StepCard
              title={t("clusterDetail.voyager.migrateSchema.schemaImportTargetDB")}
              showTooltip
            >
              {(isDone) =>
                !isDone ? (
                  <>
                    <Prereqs items={importSchemaPrereqs} />
                    <StepDetails
                      heading={t("clusterDetail.voyager.migrateSchema.schemaImport")}
                      message={t("clusterDetail.voyager.migrateSchema.schemaImportDesc")}
                      docsText={t("clusterDetail.voyager.migrateSchema.schemaImportLearn")}
                      docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#import-schema"
                    />
                  </>
                ) : null
              }
            </StepCard>
          </Box>
        </>
      )}
    </Box>
  );
};
