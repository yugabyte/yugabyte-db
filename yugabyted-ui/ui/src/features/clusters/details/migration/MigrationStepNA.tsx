import React, { FC } from "react";
import { Box, Typography } from "@material-ui/core";

interface MigrationStepNAProps {
  text?: string;
}

export const MigrationStepNA: FC<MigrationStepNAProps> = ({ text }) => {
  return (
    <Box mb={1}>
      <Typography variant="body2" color="textSecondary">
        {text ?? "N/A"}
      </Typography>
    </Box>
  );
};
