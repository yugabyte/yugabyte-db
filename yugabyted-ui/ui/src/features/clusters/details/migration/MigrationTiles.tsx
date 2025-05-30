import React, { FC } from "react";
import { Typography, makeStyles, Box } from "@material-ui/core";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import clsx from "clsx";
import { YBTooltip } from "@app/components";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";
import {
  MigrateSchemaTaskInfo,
  MigrationAssessmentReport,
  useGetMigrationAssessmentInfoQuery,
  useGetVoyagerDataMigrationMetricsQuery,
  useGetVoyagerMigrateSchemaTasksQuery,
} from "@app/api/src";
import type { Migration } from "./MigrationOverview";
import CaretRightIcon from "@app/assets/caret-right.svg";

const useStyles = makeStyles((theme) => ({
  wrapper: {
    display: "flex",
    flexDirection: "row",
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
      boxShadow: `inset 0 -3px ${theme.palette.grey[300]}`,
    },
  },
  tileDisabled: {
    color: theme.palette.grey[500],
    cursor: "not-allowed",
    "&:hover": {
      backgroundColor: "transparent",
      boxShadow: "none",
    },
  },
  tileSelected: {
    boxShadow: `inset 0 -3px ${theme.palette.grey[600]}`,
    "&:hover": {
      boxShadow: `inset 0 -3px ${theme.palette.grey[600]}`,
    },
  },
  emptyIcon: {
    backgroundColor: theme.palette.common.white,
    boxShadow: `inset 0px 0px 0px 2px ${theme.palette.grey[100]}`,
  },
  badge: {
    height: "32px",
    width: "32px",
    borderRadius: "100%",
  },
}));

interface MigrationTilesProps {
  steps: string[];
  currentStep?: number;
  phase?: number;
  migration: Migration | undefined;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
}

export const MigrationTiles: FC<MigrationTilesProps> = ({
  steps,
  onStepChange,
  currentStep,
  phase,
  migration,
  isFetching = false,
  isNewMigration = false,
}) => {
  const classes = useStyles();

  const { data: migrationAssessmentData } = useGetMigrationAssessmentInfoQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });

  const { data: migrationSchemaData } = useGetVoyagerMigrateSchemaTasksQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });

  const { data: migrationMetricsData } = useGetVoyagerDataMigrationMetricsQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });

  const { data: newMigrationAPIData } = useGetMigrationAssessmentInfoQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });

  const mNewAssessment = newMigrationAPIData as MigrationAssessmentReport | undefined;

  const mAssessmentData = migrationAssessmentData as MigrationAssessmentReport;
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
      {steps.map((step, stepIndex, allSteps) => {
        let disabled = false;
        let notStarted = false;
        let completed = false;
        let running = false;

        if (isNewMigration) {
            if (stepIndex === MigrationStep["Verification"]) {
              disabled = true;
            } else {
              notStarted = true;
            }
        } else if (phase != null) {
          if (phase === MigrationPhase["Verify"]) {
            // Everything will be completed
            completed = true;
          } else {
            // We have not reached the verify phase
            if (stepIndex === MigrationStep["Assessment"]) {
              if (
                mAssessmentData?.assessment_status === true ||
                mNewAssessment?.summary?.migration_complexity
              ) {
                completed = true;
              } else {
                notStarted = true;
              }
            } else if (stepIndex === MigrationStep["Schema Migration"]) {
              if (mSchemaData?.overall_status === "complete") {
                completed = true;
              } else if (
                mSchemaData &&
                (mSchemaData.export_schema !== "N/A" ||
                  mSchemaData.analyze_schema !== "N/A" ||
                  mSchemaData.import_schema !== "N/A")
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
        <>
          <Box
            key={step}
            className={clsx(
              classes.tile,
              currentStep === stepIndex && classes.tileSelected,
              disabled && classes.tileDisabled
            )}
            onClick={() => !disabled && onStepChange?.(stepIndex)}
          >
            {isFetching ? (
              <Box className={clsx(
                classes.badge,
                classes.emptyIcon, currentStep === stepIndex && classes.tileSelected)}
              />
            ) : (
              <>
                {completed && (
                  <YBBadge
                    className={classes.badge}
                    text=""
                    variant={BadgeVariant.Success}
                  />
                )}
                {running && (
                  <YBBadge
                    className={classes.badge}
                    text=""
                    variant={BadgeVariant.InProgress}
                  />
                )}
                {(notStarted || disabled) && (
                  <Box className={clsx(classes.badge, classes.emptyIcon)}
                  />
                )}
              </>
            )}
            <Box display="flex" alignItems="center" gridGap={10}>
              <Typography variant="body2">{step}</Typography>
              {tooltip && (
                <Box ml={-1}>
                  <YBTooltip title={tooltip} />
                </Box>
              )}
            </Box>
          </Box>
          {allSteps.length - 1 > stepIndex && (
            <Box display={"flex"} alignItems={"center"}>
              <CaretRightIcon/>
            </Box>
          )}
          </>
        );
      })}
    </Box>
  );
};
