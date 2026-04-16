import React, { FC } from "react";
import { Box, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
interface MigrationStepNAProps {
  text?: string;
}

export const MigrationStepNA: FC<MigrationStepNAProps> = ({ text }) => {
  const { t } = useTranslation();
  return (
    <Box mb={1}>
      <Typography variant="body2" color="textSecondary">
        {text ?? t("common.notAvailable")}
      </Typography>
    </Box>
  );
};
