import React, { FC } from "react";
import { Box, Typography, makeStyles } from "@material-ui/core";
import { RefactoringTables } from "./RefactoringTables";
import type { UnsupportedSqlInfo } from "@app/api/src";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[500],
  },
}));

interface ReviewRecommendedTabProps {}

export const ReviewRecommendedTab: FC<ReviewRecommendedTabProps> = ({}) => {
  const classes = useStyles();

  const unsupportedDataTypes: UnsupportedSqlInfo[] = [];
  const unsupportedFeatures: UnsupportedSqlInfo[] = [];
  const unsupportedFunctions: UnsupportedSqlInfo[] = [];

  return (
    <Box>
      {unsupportedDataTypes.length === 0 &&
      unsupportedFeatures.length === 0 &&
      unsupportedFunctions.length === 0 ? (
        <Typography variant="body2" className={classes.label}>
          No recommendations available
        </Typography>
      ) : (
        <RefactoringTables
          unsupportedDataTypes={unsupportedDataTypes}
          unsupportedFeatures={unsupportedFeatures}
          unsupportedFunctions={unsupportedFunctions}
        />
      )}
    </Box>
  );
};
