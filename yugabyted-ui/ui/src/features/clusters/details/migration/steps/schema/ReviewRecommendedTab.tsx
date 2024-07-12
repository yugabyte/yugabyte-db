import React, { FC } from "react";
import { Box, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";

const useStyles = makeStyles((theme) => ({}));

interface ReviewRecommendedTabProps {}

export const ReviewRecommendedTab: FC<ReviewRecommendedTabProps> = ({}) => {
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation();

  return <Box>Review Recommended</Box>;
};
