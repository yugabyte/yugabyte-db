import React, { FC } from "react";
import { Box, Grid, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { RefactoringGraph } from "./RefactoringGraph";
import type { SchemaAnalysisData } from "./SchemaAnalysis";

const useStyles = makeStyles((theme) => ({
  stat: {
    display: "flex",
    gap: theme.spacing(6),
    paddingRight: theme.spacing(6),
    marginRight: theme.spacing(2),
    borderRight: `1px solid ${theme.palette.grey[300]}`,
  },
  label: {
    textTransform: "uppercase",
    textAlign: "left",
    fontSize: '11.5px',
    fontWeight: 500,
    color: '#6D7C88',
  },
  muted: {
    color: theme.palette.grey[500],
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

interface SummaryTabProps {
  analysis: SchemaAnalysisData;
}

export const SummaryTab: FC<SummaryTabProps> = ({ analysis }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { automaticDDLImport, manualRefactoring } = analysis.summary.graph.reduce(
    (acc, curr) => {
      acc.automaticDDLImport += curr.automatic ?? 0;
      acc.manualRefactoring += curr.manual ?? 0;
      return acc;
    },
    { automaticDDLImport: 0, manualRefactoring: 0 }
  );


  const total = automaticDDLImport + manualRefactoring;
  return (
    <Box>
      <Grid container>
        <Box>
          <Box className={classes.stat}>
            <Box>
              <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                {t("clusterDetail.voyager.migrateSchema.automaticDDLImport")}
              </Typography>
              <Typography variant="h4" className={classes.value}>
                {automaticDDLImport}
              </Typography>
            </Box>

            <Box>
              <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                {t("clusterDetail.voyager.migrateSchema.manualRefactoring")}
              </Typography>
              <Typography variant="h4" className={classes.value}>
                {manualRefactoring}
              </Typography>
            </Box>
          </Box>
        </Box>
        <Box>
          <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
            {t("clusterDetail.voyager.migrateSchema.totalAnalyzed")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {total}
          </Typography>
        </Box>
      </Grid>

      {analysis.summary.graph.length > 0 && total > 0 && (
        <RefactoringGraph sqlObjects={analysis.summary.graph}
          sqlObjectsList={[
            ...(analysis?.reviewRecomm?.assessmentIssues ?? [])
          ]}
          isAssessmentPage={false}
        />
      )}
    </Box>
  );
};
