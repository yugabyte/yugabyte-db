import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation, Trans } from "react-i18next";
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
}

const getComplexityTooltipKey = (complexity: string) => {
  const complexityTooltipKey: {[key: string]: string} = {
    "high": "clusterDetail.voyager.planAndAssess.summary.complexityTooltipHard",
    "medium": "clusterDetail.voyager.planAndAssess.summary.complexityTooltipMedium",
    "low": "clusterDetail.voyager.planAndAssess.summary.complexityTooltipEasy",
  };
  return complexityTooltipKey[complexity] ?? complexity;
}

export const MigrationAssessmentSummary: FC<MigrationAssessmentSummaryProps> = ({
  complexity,
  estimatedMigrationTime,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

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
                  <Trans
                    i18nKey={getComplexityTooltipKey(complexity.toLowerCase())}
                    components={{bold: <Typography variant="body1" display="inline"/>}}
                  />
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
