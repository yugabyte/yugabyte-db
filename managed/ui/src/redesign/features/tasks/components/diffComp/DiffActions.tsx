/*
 * Created on Fri Jun 07 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { Grid, Typography } from '@material-ui/core';
import { YBButton } from '../../../../components';
import { useTranslation } from 'react-i18next';

interface DiffActionsProps {
  onExpandAll: (flag: boolean) => void;
  changesCount: number;
}

export const DiffActions: FC<DiffActionsProps> = ({ onExpandAll, changesCount }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.diffModal'
  });
  const [expanded, setExpanded] = useState(false);

  return (
    <Grid container spacing={2} justifyContent="space-between" alignItems="center">
      <Grid item>
        <Typography variant="h5">{changesCount} changes</Typography>
      </Grid>
      <Grid item>
        <YBButton
          onClick={() => {
            setExpanded(!expanded);
            onExpandAll(!expanded);
          }}
          variant="secondary"
          data-testid="diff-expand-all"
        >
          {expanded ? t('collapseAll') : t('expandAll')}
        </YBButton>
      </Grid>
    </Grid>
  );
};
