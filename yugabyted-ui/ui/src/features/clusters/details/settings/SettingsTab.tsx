import React, { FC } from 'react';
import { Box, makeStyles } from '@material-ui/core';
import { GeneralOverview } from './GeneralOverview';
import { RegionOverview } from './RegionOverview';
import { GFlagsOverview } from './GFlagsOverview';
import { GFlagsDrift } from './GFlagsDrift';

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

  const [showDrift, setShowDrift] = React.useState<boolean>(false);

  return (
    <Box className={classes.sectionContainer}>
      <GeneralOverview />
      <RegionOverview />
      <RegionOverview readReplica />
      <GFlagsOverview showDrift={showDrift} 
        toggleDrift={() => setShowDrift(s => !s)} />
      {showDrift && <GFlagsDrift />}
    </Box>
  );
};
