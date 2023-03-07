import React, { FC } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import { GeneralOverview } from './GeneralOverview';
import { RegionOverview } from './RegionOverview';
import { GFlagsOverview } from './GFlagsOverview';

const useStyles = makeStyles((theme) => ({
  sectionContainer: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(2),
  }
}));

interface SettingsTabProps {
}

export const SettingsTab: FC<SettingsTabProps> = () => {
  const classes = useStyles();

  return (
    <Box className={classes.sectionContainer}>
      <GeneralOverview />
      <RegionOverview />
      <GFlagsOverview />
    </Box>
  );
};
