import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { RefactoringCount, UnsupportedSqlInfo } from "@app/api/src";
import { RefactoringGraph } from "../schema/RefactoringGraph";
import { RefactoringTables } from "../schema/RefactoringTables";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(3),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.25),
    textTransform: "uppercase",
    textAlign: "left",
  },
  fullWidth: {
    width: "100%",
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
}));

interface MigrationAssessmentRefactoringProps {
  sqlObjects: RefactoringCount[] | undefined;
  unsupportedDataTypes: UnsupportedSqlInfo[] | undefined;
  unsupportedFeatures: UnsupportedSqlInfo[] | undefined;
  unsupportedFunctions: UnsupportedSqlInfo[] | undefined;
}

export const MigrationAssessmentRefactoring: FC<MigrationAssessmentRefactoringProps> = ({
  sqlObjects,
  unsupportedDataTypes,
  unsupportedFeatures,
  unsupportedFunctions,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

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
            {t("clusterDetail.voyager.planAndAssess.refactoring.heading")}
          </Typography>
        </Box>

        <RefactoringGraph sqlObjects={sqlObjects} />

        {unsupportedDataTypes?.length ||
        unsupportedFeatures?.length ||
        unsupportedFunctions?.length ? (
          <>
            <Divider />

            <Box my={3}>
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.refactoring.conversionIssues")}
              </Typography>
            </Box>

            <RefactoringTables
              unsupportedDataTypes={unsupportedDataTypes}
              unsupportedFeatures={unsupportedFeatures}
              unsupportedFunctions={unsupportedFunctions}
            />
          </>
        ) : null}
      </Box>
    </Paper>
  );
};
