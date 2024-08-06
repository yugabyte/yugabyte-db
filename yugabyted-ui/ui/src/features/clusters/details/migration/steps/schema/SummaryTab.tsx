import React, { FC } from "react";
import { Box, Grid, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import clsx from "clsx";
import { RefactoringGraph } from "./RefactoringGraph";

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
  statLabel: {
    marginBottom: theme.spacing(0.75),
  },
  value: {
    color: theme.palette.grey[700],
    paddingTop: theme.spacing(0.57),
    textAlign: "left",
  },
}));

interface SummaryTabProps {}

export const SummaryTab: FC<SummaryTabProps> = ({}) => {
  const classes = useStyles();
  const { t } = useTranslation();

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
                113
              </Typography>
            </div>

            <div>
              <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
                {t("clusterDetail.voyager.migrateSchema.manualRefactoring")}
              </Typography>
              <Typography variant="h4" className={classes.value}>
                32
              </Typography>
            </div>
          </div>
        </div>
        <div>
          <Typography variant="body1" className={clsx(classes.label, classes.statLabel)}>
            {t("clusterDetail.voyager.migrateSchema.totalAnalyzed")}
          </Typography>
          <Typography variant="h4" className={classes.value}>
            {113 + 32}
          </Typography>
        </div>
      </Grid>

      <RefactoringGraph
        sqlObjects={[
          {
            sql_object_type: "sql_type",
            automatic: 14,
            manual: 2,
          },
          {
            sql_object_type: "table",
            automatic: 21,
            manual: 0,
          },
        ]}
      />
    </Box>
  );
};
