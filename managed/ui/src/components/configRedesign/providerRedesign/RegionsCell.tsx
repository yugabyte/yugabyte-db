/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { Box, Typography } from '@material-ui/core';

import { usePillStyles } from '../../../redesign/styles/styles';

import { YBRegion } from './types';

// Region cell for providers
interface RegionsCellProps {
  regions: YBRegion[];
}

export const RegionsCell = ({ regions }: RegionsCellProps) => {
  const classes = usePillStyles();
  if (regions.length === 0) {
    return null;
  }

  const sortedRegions = regions.sort((a, b) => (a.code > b.code ? 1 : -1));
  const firstRegion = sortedRegions[0];

  return (
    <Box display="flex" alignItems="center" gridGap="5px">
      <Typography variant="body2">{firstRegion.name}</Typography>
      {sortedRegions.length > 1 && <div className={classes.pill}>+{sortedRegions.length - 1}</div>}
    </Box>
  );
};
