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
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
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

  const automaticDDLImport = analysis.summary.graph.reduce(
    (prev, curr) => prev + (curr.automatic ?? 0),
    0
  );

  const manualRefactoring = analysis.summary.graph.reduce(
    (prev, curr) => prev + (curr.manual ?? 0),
    0
  );

  const total = automaticDDLImport + manualRefactoring;

  return (
    <Box>
      <Grid container>
        <div>
          <div className={classes.stat}>
            <div>
              <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                {t("clusterDetail.voyager.migrateSchema.automaticDDLImport")}
              </Typography>
              <Typography variant="h4" className={classes.value}>
                {automaticDDLImport}
              </Typography>
            </div>

            <div>
              <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                {t("clusterDetail.voyager.migrateSchema.manualRefactoring")}
              </Typography>
              <Typography variant="h4" className={classes.value}>
                {manualRefactoring}
              </Typography>
            </div>
          </div>
        </div>
        <div>
          <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
            {t("clusterDetail.voyager.migrateSchema.totalAnalyzed")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {total}
          </Typography>
        </div>
      </Grid>

      {analysis.summary.graph.length > 0 && total > 0 && (
        <RefactoringGraph sqlObjects={analysis.summary.graph} />
      )}
    </Box>
  );
};
