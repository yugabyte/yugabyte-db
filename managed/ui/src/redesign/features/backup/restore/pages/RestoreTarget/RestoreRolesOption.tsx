/*
 * Created on Wed Dec 15 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Theme, Typography } from '@material-ui/core';
import { AlertVariant, YBAlert } from '@yugabyte-ui-library/core';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { YBCheckboxField, YBTooltip } from '../../../../../components';

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    height: '90px',
    width: '100%',
    display: 'flex',
    flexDirection: 'column',
    padding: '12px 16px 12px 16px',
    border: '1px solid #E9EEF2',
    borderRadius: '8px'
  },
  disabled: {
    opacity: 0.6,
    color: theme.palette.grey[600]
  },
  tooltipContainer: {
    width: '276px'
  },
  alertWithTopAlignedIcon: {
    '& > *:first-child': {
      alignSelf: 'self-start',
      marginTop: '-4px'
    }
  }
}));

interface RestoreRolesOptionProps {
  disabled: boolean;
}

export const RestoreRolesOption: FC<RestoreRolesOptionProps> = ({
  disabled
}: RestoreRolesOptionProps) => {
  const { control, watch, setValue } = useFormContext<RestoreFormModel>();
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.source'
  });

  const useRoles = watch('target.useRoles');

  const header = (
    <Typography variant="body1" color={'textPrimary'}>
      {t('restoreRoles')}
    </Typography>
  );
  const content = (
    <div>
      <Box className={`${classes.root} ${disabled ? classes.disabled : ''}`} mt={2}>
        <YBCheckboxField
          label={t('restoreRoles')}
          control={control}
          name="target.useRoles"
          checked={useRoles}
          disabled={disabled}
          onChange={(event) => {
            if (!disabled) {
              setValue('target.useRoles', event.target.checked);
            }
          }}
        />
        <Box mt={1} ml={4}>
          <Typography variant="body2" color="textSecondary">
            {t('restoreRolesHelpText')}
          </Typography>
        </Box>
      </Box>
    </div>
  );

  if (disabled) {
    return (
      <YBTooltip
        classes={{ tooltip: classes.tooltipContainer }}
        title={
          <Box display={'flex'} flexDirection={'column'}>
            <Box>
              <b>{t('restoreRolesTooltip1')}</b>
            </Box>
            <Box>{t('restoreRolesTooltip2')}</Box>
          </Box>
        }
        placement="top"
        arrow
        PopperProps={{ style: { zIndex: 99999 } }}
      >
        <span style={{ width: '100%', cursor: 'not-allowed', pointerEvents: 'auto' }}>
          {header}
          {content}
        </span>
      </YBTooltip>
    );
  }

  return (
    <>
      {header}
      {content}
      {useRoles && (
        <Box mt={2}>
          <YBAlert
            open
            variant={AlertVariant.Info}
            className={classes.alertWithTopAlignedIcon}
            text={
              <Box display={'flex'} flexDirection={'column'}>
                <Box>
                  <b>{t('restoreRolesOnMsg1')}</b>
                </Box>
                <Box display="flex" mt={0.5}>
                  <Typography component="span" style={{ marginRight: '8px' }}>
                    {'•'}
                  </Typography>
                  <Typography component="span" variant="body2">
                    {t('restoreRolesOnMsg2')}
                  </Typography>
                </Box>
                <Box display="flex" mt={0.5}>
                  <Typography component="span" style={{ marginRight: '8px' }}>
                    {'•'}
                  </Typography>
                  <Typography component="span" variant="body2">
                    {t('restoreRolesOnMsg3')}
                  </Typography>
                </Box>
              </Box>
            }
          />
        </Box>
      )}
    </>
  );
};
