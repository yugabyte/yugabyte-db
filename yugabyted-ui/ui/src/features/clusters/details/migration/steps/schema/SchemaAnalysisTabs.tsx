import React, { FC } from "react";
import { Box, makeStyles, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { SummaryTab } from "./SummaryTab";
import clsx from "clsx";
import type { SchemaAnalysisData } from "./SchemaAnalysis";

const useStyles = makeStyles((theme) => ({
  fullWidth: {
    width: "100%",
  },
  nmt: {
    marginTop: theme.spacing(-1),
  },
  heading: {
    paddingTop: '24px',
    paddingBottom: '24px',
    paddingLeft: '16px',
    paddingRight: '16px',
    margin: '0 -15px',
    width: 'calc(100% + 30px)',
  },
}));

type SchemaAnalysisTabsProps = {
  analysis: SchemaAnalysisData;
};

export const SchemaAnalysisTabs: FC<SchemaAnalysisTabsProps> = ({ analysis }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Box className={clsx(classes.fullWidth, classes.nmt)} style={{ padding: 0, margin: 0 }}>
      <Typography
        variant="h5"
        component="h5"
        className={classes.heading}
      >
        {t("clusterDetail.voyager.migrateSchema.tabSuggestedRefactoring")}
      </Typography>

      <Box mt={3}>
        <SummaryTab analysis={analysis} />
      </Box>
    </Box>
  );
};
