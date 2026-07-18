import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import {
  ComplexityComponent,
  getComplexityString,
} from "../../ComplexityComponent";
import HelpIcon from "@app/assets/help-new.svg";
import { YBTooltip } from "@app/components";
import { MetadataItem } from "../../components/MetadataItem";

const useStyles = makeStyles((theme) => ({
  heading: {
    display: "flex",
    justifyContent: "space-between",
    alignItems: "center",
    padding: 24,
    flex: "0 0 auto"
  },
  label: {
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
    fontSize: '11.5px',
    fontWeight: 500,
    color: '#6D7C88',
  },
  migrationComplexityLabel: {
    color: '#6D7C88',
    fontFamily: 'Inter',
    fontSize: '11.5px',
    fontStyle: 'normal',
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase',
    marginBottom: theme.spacing(1),
  },
  paper: {
    overflow: "clip",
    border: "1px solid #E9EEF2",
    borderRadius: theme.shape.borderRadius,
    height: "100%",
    display: "flex",
    flexDirection: "column"
  },
  boxBody: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    paddingTop: 24,
    paddingBottom: 24,
    paddingRight: 24,
    paddingLeft: 24,
    flex: 1,
    height: "100%"
  },
  icon: {
    display: "flex",
    cursor: "pointer",
    color: theme.palette.grey[500],
    alignItems: "center",
    width: 20,
    height: 20,
  },
  complexityText: {
    fontFamily: 'Inter',
    fontSize: '11.5px',
    fontStyle: 'normal',
    fontWeight: 500,
    lineHeight: '16px',
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

  // Making the Hard/Medium/Easy migration phrases bold
  const finalTranslationOutput = migCompexityExp?.replace(
    /(Hard|Medium|Easy)\s+migration/g,
    (match) => `<strong>${match}</strong>`
  );

  const renderTranslationOutput = (
    <Typography variant="body2" className={classes.complexityText}>
      <span dangerouslySetInnerHTML={{ __html: finalTranslationOutput || '' }} />
    </Typography>
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
      <Box className={classes.heading}>
        <Typography variant="h5">
          {t("clusterDetail.voyager.planAndAssess.summary.heading")}
        </Typography>
      </Box>

      <Divider orientation="horizontal" />

      <Box className={classes.boxBody}>
        <Box>
          <Typography className={classes.migrationComplexityLabel}>
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
                  {renderTranslationOutput}
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
          <MetadataItem
            label={[t("clusterDetail.voyager.planAndAssess.summary.estimatedDataMigrationTime")]}
            value={[convertTime(estimatedMigrationTime)]}
          />
        </Box>
      </Box>
    </Paper>
  );
};
