import React, { FC } from "react";
import { Typography, makeStyles, Box, Card, CardActionArea } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { STATUS_TYPES, YBStatus, YBTooltip } from "@app/components";
import { MigrationPhase, MigrationStep, migrationPhases, migrationSteps } from "./migration";
import {
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
  useGetVoyagerMigrationAssesmentDetailsQuery,
} from "@app/api/src";
import type { Migration } from "./MigrationOverview";

const useStyles = makeStyles((theme) => ({
  wrapper: {
    margin: theme.spacing(1, 0, 1, 0),
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(4),
    flexWrap: "wrap",
  },
  heading: {
    margin: theme.spacing(0, 0, 0, 0),
  },
  icon: {
    height: theme.spacing(4),
    width: theme.spacing(4),
    borderRadius: "50%",
    backgroundColor: theme.palette.grey[300],
    color: theme.palette.grey[900],
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    fontSize: "15px",
    lineHeight: "18px",
    fontWeight: 700,
  },
  highlight: {
    color: theme.palette.primary["700"],
    fontWeight: 600,
  },
  card: {
    boxShadow: "none",
  },
  cardActionArea: {
    padding: theme.spacing(2),
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    alignItems: "start",
    position: "relative",
  },
  selected: {
    background: theme.palette.grey[100],
  },
  notStarted: {
    color: theme.palette.grey[700],
  },
  disabledOverlay: {
    position: "absolute",
    top: 0,
    right: 0,
    width: "100%",
    height: "100%",
    background: "rgba(255, 255, 255, 0.5)",
    pointerEvents: "none",
  },
  disabledCard: {
    borderColor: theme.palette.grey[200],
    "&:hover": {
      cursor: "not-allowed",
    },
  },
}));

interface MigrationTilesProps {
  steps: string[];
  currentStep?: number;
  phase?: number;
  migration: Migration;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
}

export const MigrationTiles: FC<MigrationTilesProps> = ({
  steps,
  onStepChange,
  currentStep,
  phase,
  migration,
  isFetching = false,
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  const { data: migrationAssessmentData } = useGetVoyagerMigrationAssesmentDetailsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const { data: migrationSchemaData } = useGetVoyagerMigrateSchemaTasksQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const { data: migrationMetricsData } = useGetVoyagerDataMigrationMetricsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const getTooltip = (step: string) => {
    if (step === migrationSteps[MigrationStep["Plan and Assess"]]) {
      return migrationPhases[MigrationPhase["Analyze Schema"]];
    } else if (step === migrationSteps[MigrationStep["Migrate Schema"]]) {
      return [
        migrationPhases[MigrationPhase["Analyze Schema"]],
        migrationPhases[MigrationPhase["Export Schema"]],
        migrationPhases[MigrationPhase["Import Schema"]],
      ].join(", ");
    } else if (step === migrationSteps[MigrationStep["Migrate Data"]]) {
      return [
        migrationPhases[MigrationPhase["Export Data"]],
        migrationPhases[MigrationPhase["Import Data"]],
      ].join(", ");
    } else if (step === migrationSteps[MigrationStep["Verify"]]) {
      return migrationPhases[MigrationPhase["Verify"]];
    } else {
      return step;
    }
  };

  return (
    <Box className={classes.wrapper}>
      {steps.map((step, stepIndex) => {
        let disabled = false;
        let notStarted = false;
        let completed = false;
        let running = false;

        if (phase != null) {
          // Export schema phase
          if (phase === MigrationPhase["Export Schema"]) {
            if (
              stepIndex === MigrationStep["Migrate Data"] ||
              stepIndex === MigrationStep["Verify"]
            ) {
              // Migrate data and Verify will be pending and disabled
              notStarted = true;
              disabled = true;
            } else if (stepIndex === MigrationStep["Plan and Assess"]) {
              // Plan and assess will be pending
              notStarted = true;
            } else if (stepIndex === MigrationStep["Migrate Schema"]) {
              // Migrate schema will be running
              running = true;
            }
          }

          // Analyze schema phase
          else if (phase === MigrationPhase["Analyze Schema"]) {
            if (
              stepIndex === MigrationStep["Migrate Data"] ||
              stepIndex === MigrationStep["Verify"]
            ) {
              // Migrate data and Verify will be pending and disabled
              disabled = true;
              notStarted = true;
            } else if (stepIndex === MigrationStep["Migrate Schema"]) {
              // Migrate schema will be running
              running = true;
            } else if (stepIndex === MigrationStep["Plan and Assess"]) {
              // Plan and assess will be completed
              completed = true;
            }
          }

          // Export data and Import schema phase
          else if (
            phase === MigrationPhase["Export Data"] ||
            phase === MigrationPhase["Import Schema"]
          ) {
            if (stepIndex === MigrationStep["Plan and Assess"]) {
              // Plan and assess will be completed
              completed = true;
            } else if (
              stepIndex === MigrationStep["Migrate Schema"] ||
              stepIndex === MigrationStep["Migrate Data"]
            ) {
              // Migrate schema and Migrate data will be running
              running = true;
            } else if (stepIndex === MigrationStep["Verify"]) {
              // Verify will be pending and disabled
              disabled = true;
              notStarted = true;
            }
          }

          // Import data phase
          else if (phase === MigrationPhase["Import Data"]) {
            if (stepIndex <= MigrationStep["Migrate Schema"]) {
              // Plan and assess and Migrate schema will be completed
              completed = true;
            } else if (stepIndex === MigrationStep["Migrate Data"]) {
              // Migrate data will be running
              running = true;
            } else if (stepIndex === MigrationStep["Verify"]) {
              // Verify will be pending and disabled
              disabled = true;
              notStarted = true;
            }
          }

          // Verify phase
          else if (phase === MigrationPhase["Verify"]) {
            // Everything will be completed
            completed = true;
          }

          // Extra check overrides based on other migration APIs
          if (stepIndex === MigrationStep["Plan and Assess"]) {
            if (migrationAssessmentData?.data?.assesment_status === true) {
              notStarted = false;
              disabled = false;
              completed = true;
            }
          } else if (stepIndex === MigrationStep["Migrate Schema"]) {
            if (migrationSchemaData?.data?.overall_status === "complete") {
              notStarted = false;
              disabled = false;
              completed = true;
            }
          } else if (stepIndex === MigrationStep["Migrate Schema"]) {
            if (!migrationMetricsData?.metrics?.length) {
              notStarted = true;
            } else {
              const importedMetrics = migrationMetricsData?.metrics
                ?.filter((metrics) => metrics.migration_phase === MigrationPhase["Import Data"])
                .map((data) => ({
                  importPercentage:
                    data.count_live_rows && data.count_total_rows
                      ? Math.floor((data.count_live_rows / data.count_total_rows) * 100)
                      : 0,
                }));

              if (
                importedMetrics &&
                Math.floor(
                  importedMetrics.reduce((acc, { importPercentage }) => acc + importPercentage, 0) /
                    (importedMetrics.length || 1)
                ) === 100
              ) {
                notStarted = false;
                disabled = false;
                completed = true;
              }
            }
          }
        }

        return (
          <Card key={step} className={clsx(classes.card, disabled && classes.disabledCard)}>
            <CardActionArea
              className={clsx(
                classes.cardActionArea,
                currentStep === stepIndex && classes.selected
              )}
              disabled={disabled}
              onClick={() => onStepChange && onStepChange(stepIndex)}
            >
              {disabled && <Box className={classes.disabledOverlay} />}
              <Box display="flex" alignItems="center" gridGap={10}>
                {/* <span className={classes.icon}>{(index + 1).toString()}</span> */}
                <Typography variant="h5">{step}</Typography>
                <Box ml={-1}>
                  <YBTooltip title={getTooltip(step)} />
                </Box>
              </Box>
              {isFetching ? (
                <YBStatus
                  type={STATUS_TYPES.IN_PROGRESS}
                  label={t("clusterDetail.voyager.refreshing")}
                />
              ) : (
                <>
                  {completed && (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      text={t("clusterDetail.voyager.complete")}
                    />
                  )}
                  {running && <YBBadge variant={BadgeVariant.InProgress} />}
                  {notStarted && (
                    <Box mt={0.8} mb={0.4} className={classes.notStarted}>
                      {t("clusterDetail.voyager.notStarted")}
                    </Box>
                  )}
                </>
              )}
            </CardActionArea>
          </Card>
        );
      })}
    </Box>
  );
};
