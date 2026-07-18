import React from "react";
import { Box, useTheme } from "@material-ui/core";

const MigrationAccordionTitle = ({
  title,
  count,
  color,
}: {
  title: string;
  count?: number;
  color?: string;
}) => {
  const theme = useTheme();

  return (
    <Box display="flex" alignItems="center" gridGap={theme.spacing(1)} flexGrow={1}>
      {title}
      {count ? (
        <Box
          borderRadius={theme.shape.borderRadius}
          bgcolor={color || theme.palette.info[100]}
          fontSize="12px"
          fontWeight={400}
          px="5px"
          py="2px"
        >
          {count}
        </Box>
      ) : null}
    </Box>
  );
};

export default MigrationAccordionTitle;
