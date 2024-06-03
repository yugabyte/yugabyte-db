import React, { FC } from "react";
import { Box, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { useGetVoyagerMigrationAssesmentDetailsQuery } from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";
import { newMigration } from "./AssessmentData";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(2),
  },
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
  },
  nextSteps: {
    paddingLeft: theme.spacing(4),
    marginBottom: theme.spacing(4),
  },
  hardComp: {
    color: theme.palette.error.main,
  },
  mediumComp: {
    color: theme.palette.warning[700],
  },
  easyComp: {
    color: theme.palette.success.main,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textAlign: "left",
  },
}));

interface MigrationAssessmentProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
}

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  heading,
  migration,
  onRefetch,
  onStepChange,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  // DATO
  const {
    data: dato,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentDetailso,
  } = useGetVoyagerMigrationAssesmentDetailsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center">
        <Typography variant="h4" className={classes.heading}>
          {t("clusterDetail.voyager.planAndAssess.heading")}
        </Typography>
        {newMigration.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigration.completedTime}
          </Typography>
        )}
      </Box>

      <MigrationAssessmentSummary
        {...newMigration.summary}
        complexity={migration.complexity || newMigration.summary.complexity}
        estimatedMigrationTime={
          newMigration.Sizing.SizingRecommendation.EstimatedTimeInMinForImport
        }
      />

      <MigrationSourceEnv {...newMigration.sourceEnv} />

      <MigrationAssessmentRecommendation data={newMigration} />

      <MigrationAssessmentRefactoring
        sqlObjects={newMigration.SchemaSummary.DatabaseObjects}
        unsupportedDataTypes={newMigration.UnsupportedDataTypes}
        unsupportedFeatures={newMigration.UnsupportedFeatures}
      />
    </Box>
  );
};
