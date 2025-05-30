import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import {
  ComplexityComponent,
  getComplexityString,
} from "../../ComplexityComponent";
import HelpIcon from "@app/assets/help.svg";
import { YBTooltip } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
    paddingTop: theme.spacing(3),
    paddingBottom: theme.spacing(3),
    paddingRight: theme.spacing(3),
    paddingLeft: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
  },
  paper: {
    overflow: "clip",
    height: "100%",
  },
  boxBody: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    height: "100%",
    paddingRight: theme.spacing(3),
    paddingLeft: theme.spacing(3),
    paddingTop: theme.spacing(3),
    paddingBottom: theme.spacing(3),
  },
  icon: {
    display: "flex",
    cursor: "pointer",
    color: theme.palette.grey[500],
    alignItems: "center",
  },
}));

interface MigrationAssessmentSummaryProps {
  complexity: string;
  estimatedMigrationTime: string | number;
  migrationComplexityExplanation?: string | undefined;
}

export const MigrationAssessmentSummary: FC<MigrationAssessmentSummaryProps> = ({
  complexity,
  estimatedMigrationTime,
  migrationComplexityExplanation
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();


  // Converting High Medium Low keywords to Hard Medium Easy
  const migCompexityExp: string | undefined = migrationComplexityExplanation
    ?.replaceAll(/\b(HIGH|MEDIUM|LOW)\b/gi, (match) =>
      getComplexityString(match.toLowerCase(), t)
    )
    .replace(/(\d+)\s+Level\s+\d+\s+issue\(s\)/g, (match, number) =>
      (number === "1" || number === "0")
        ? match.replace(
          "issue(s)",
          t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issue")
        )
        : match.replace(
          "issue(s)",
          t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issues")
        )
    );

  // Making the Hard Medium Easy keywords bold
  const finalTranslationOutput =
    migCompexityExp?.split(/\b(Hard|Medium|Easy)\b/g).map((part, index) =>
      ["Hard", "Medium", "Easy"].includes(part) ? <strong key={index}>{part}</strong> : part
    );

  const convertTime = (timeInMin: string | number) => {
    if (typeof timeInMin === "string") {
      return timeInMin;
    }

    const hours = Math.floor(timeInMin / 60);
    const minutes = timeInMin % 60;

    return `${hours}h ${minutes}m`;
  };

  return (
    <Paper className={classes.paper}>
      <Box height="100%">
        <Box className={classes.heading}>
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.summary.heading")}
          </Typography>
        </Box>

        <Divider orientation="horizontal" />

        <Box className={classes.boxBody}>
          <Box>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.summary.migrationComplexity")}
            </Typography>
            <Box display="flex" gridGap={theme.spacing(2)}>
              <Typography variant="h4">
                {getComplexityString(complexity.toLowerCase(), t)}
              </Typography>
              <ComplexityComponent complexity={complexity}/>
              <YBTooltip title=
                {
                  <Box>
                    <Typography variant="body1" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.summary.migrationComplexity")}
                    </Typography>
                    <Typography variant="body2">{finalTranslationOutput}</Typography>
                  </Box>
                }
              >
                <Box className={classes.icon}>
                  <HelpIcon/>
                </Box>
              </YBTooltip>
            </Box>
          </Box>

          <Box>
            <Typography variant="body1" className={classes.label}>
              {t("clusterDetail.voyager.planAndAssess.summary.estimatedDataMigrationTime")}
            </Typography>
            <Typography variant="body2">{convertTime(estimatedMigrationTime)}</Typography>
          </Box>
        </Box>
      </Box>
    </Paper>
  );
};
