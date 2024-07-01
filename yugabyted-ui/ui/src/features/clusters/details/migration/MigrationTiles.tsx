import React, { FC } from "react";
import { Typography, makeStyles, Box } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { STATUS_TYPES, YBStatus, YBTooltip } from "@app/components";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";
import {
  MigrateSchemaTaskInfo,
  MigrationAssesmentInfo,
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
  useGetVoyagerMigrationAssesmentDetailsQuery,
} from "@app/api/src";
import type { Migration } from "./MigrationOverview";
import TodoIcon from "@app/assets/todo.svg";

const useStyles = makeStyles((theme) => ({
  wrapper: {
    display: "flex",
    flexDirection: "column",
  },
  tile: {
    padding: theme.spacing(2, 3, 2, 3),
    display: "flex",
    flexDirection: "row",
    justifyContent: "space-between",
    alignItems: "center",
    gridGap: theme.spacing(1),
    cursor: "pointer",
    transition: "background-color 0.2s",
    "&:hover": {
      backgroundColor: theme.palette.primary[100],
    },
  },
  tileDisabled: {
    color: theme.palette.grey[500],
    cursor: "not-allowed",
    "&:hover": {
      backgroundColor: "transparent",
    },
  },
  tileSelected: {
    backgroundColor: theme.palette.primary[100],
    boxShadow: `inset 5px 0 0 -1px ${theme.palette.primary[600]}`,
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

  const mAssessmentData = migrationAssessmentData as MigrationAssesmentInfo;
  const mSchemaData = migrationSchemaData as MigrateSchemaTaskInfo;

  const getTooltip = (step: string) => {
    if (step === migrationSteps[MigrationStep["Assessment"]]) {
      return ""; // Tooltip for assessment
    } else if (step === migrationSteps[MigrationStep["Schema Migration"]]) {
      return ""; // Tooltip for migrate schema
    } else if (step === migrationSteps[MigrationStep["Data Migration"]]) {
      return ""; // Tooltip for migrate data
    } else if (step === migrationSteps[MigrationStep["Verification"]]) {
      return ""; // Tooltip for verify
    } else {
      return undefined;
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
          if (phase === MigrationPhase["Verify"]) {
            // Everything will be completed
            completed = true;
          } else {
            // We have not reached the verify phase
            if (stepIndex === MigrationStep["Assessment"]) {
              if (
                mAssessmentData?.assesment_status === true ||
                (migration.migration_phase === MigrationPhase["Assess Migration"] &&
                  migration.invocation_sequence === 2)
              ) {
                completed = true;
              } else {
                notStarted = true;
              }
            } else if (stepIndex === MigrationStep["Schema Migration"]) {
              if (mSchemaData?.overall_status === "complete") {
                completed = true;
              } else if (
                mSchemaData?.export_schema !== "N/A" ||
                mSchemaData?.analyze_schema !== "N/A" ||
                mSchemaData?.import_schema !== "N/A"
              ) {
                running = true;
              } else {
                notStarted = true;
              }
            } else if (stepIndex === MigrationStep["Data Migration"]) {
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
                const importPercentage = importedMetrics
                  ? Math.floor(
                      importedMetrics.reduce(
                        (acc, { importPercentage }) => acc + importPercentage,
                        0
                      ) / (importedMetrics.length || 1)
                    )
                  : 0;

                if (importPercentage === 100) {
                  completed = true;
                } else {
                  running = true;
                }
              }
            } else if (stepIndex === MigrationStep["Verification"]) {
              // Verify will be disabled
              disabled = true;
            }
          }
        }

        const tooltip = getTooltip(step);

        return (
          <Box
            className={clsx(
              classes.tile,
              currentStep === stepIndex && classes.tileSelected,
              disabled && classes.tileDisabled
            )}
            onClick={() => !disabled && onStepChange?.(stepIndex)}
          >
            <Box display="flex" alignItems="center" gridGap={10}>
              <Typography variant="body2">{step}</Typography>
              {tooltip && (
                <Box ml={-1}>
                  <YBTooltip title={tooltip} />
                </Box>
              )}
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
                    text={t("clusterDetail.voyager.completed")}
                  />
                )}
                {running && <YBBadge variant={BadgeVariant.InProgress} />}
                {notStarted && (
                  <YBBadge
                    variant={BadgeVariant.InProgress}
                    text={t("clusterDetail.voyager.todo")}
                    iconComponent={TodoIcon}
                  />
                )}
              </>
            )}
          </Box>
        );
      })}
    </Box>
  );
};
