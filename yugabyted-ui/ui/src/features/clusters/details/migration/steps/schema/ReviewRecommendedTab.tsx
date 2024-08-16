import React, { FC } from "react";
import { Box, Typography, makeStyles } from "@material-ui/core";
import { RefactoringTables } from "./RefactoringTables";
import type { SchemaAnalysisData } from "./SchemaAnalysis";

const useStyles = makeStyles((theme) => ({
  muted: {
    color: theme.palette.grey[500],
  },
}));

interface ReviewRecommendedTabProps {
  analysis: SchemaAnalysisData;
}

export const ReviewRecommendedTab: FC<ReviewRecommendedTabProps> = ({ analysis }) => {
  const classes = useStyles();

  const unsupportedDataTypes = analysis.reviewRecomm.unsupportedDataTypes ?? [];
  const unsupportedFeatures = analysis.reviewRecomm.unsupportedFeatures ?? [];
  const unsupportedFunctions = analysis.reviewRecomm.unsupportedFunctions ?? [];

  return (
    <Box>
      {unsupportedDataTypes.length === 0 &&
      unsupportedFeatures.length === 0 &&
      unsupportedFunctions.length === 0 ? (
        <Typography variant="body2" className={classes.muted}>
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
