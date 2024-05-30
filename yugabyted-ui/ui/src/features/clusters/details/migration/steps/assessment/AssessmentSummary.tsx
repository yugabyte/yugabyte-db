import React, { FC } from "react";
import { Box, Paper, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
  },
}));

const ComplexityComponent = (complexity: string) => {
  const complexityL = complexity.toLowerCase();

  const badgeVariant =
    complexityL === "hard"
      ? BadgeVariant.Error
      : complexityL === "medium"
      ? BadgeVariant.Warning
      : complexityL === "easy"
      ? BadgeVariant.Success
      : undefined;

  return <YBBadge variant={badgeVariant} text={complexity || "N/A"} icon={false} />;
};

interface MigrationAssessmentSummaryProps {
  complexity: string;
  estimatedMigrationTime: string | number;
}

export const MigrationAssessmentSummary: FC<MigrationAssessmentSummaryProps> = ({
  complexity,
  estimatedMigrationTime,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const convertTime = (timeInMin: string | number) => {
    if (typeof timeInMin === "string") {
      return timeInMin;
    }

    const hours = Math.floor(timeInMin / 60);
    const minutes = timeInMin % 60;

    return `${hours}h ${minutes}m`;
  };

  return (
    <Paper>
      <Box px={2} py={3}>
        <Box
          display="flex"
          justifyContent="space-between"
          alignItems="center"
          className={classes.heading}
        >
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.summary.heading")}
          </Typography>
        </Box>

        <Box display="flex" justifyContent="space-between" alignItems="center">
          <Box>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.summary.migrationComplexity")}
            </Typography>
            {ComplexityComponent(complexity)}
          </Box>

          <Box>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.summary.estimatedMigrationTime")}
            </Typography>
            <Typography variant="body2">{convertTime(estimatedMigrationTime)}</Typography>
          </Box>
        </Box>

        <Box mt={2}>{t("clusterDetail.voyager.planAndAssess.summary.description")}</Box>
      </Box>
    </Paper>
  );
};
