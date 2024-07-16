import React, { FC } from "react";
import { Box } from "@material-ui/core";
import { RefactoringTables } from "./RefactoringTables";

interface ReviewRecommendedTabProps {}

export const ReviewRecommendedTab: FC<ReviewRecommendedTabProps> = ({}) => {
  return (
    <Box>
      <RefactoringTables
        unsupportedDataTypes={[]}
        unsupportedFeatures={[]}
        unsupportedFunctions={[]}
      />
    </Box>
  );
};
