import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { mui, YBToggleField, YBTooltip, YBLabel } from '@yugabyte-ui-library/core';
import { YBEarlyAccessTag } from '../../../../components';
// import { isVersionConnectionPoolSupported } from '../../../../features/universe/universe-form/utils/helpers';

import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YSQL_FIELD } from '../ysql-settings/YSQLSettingsField';

const { Box, Typography } = mui;

interface ConnectionPoolFieldProps {
  disabled: boolean;
}

const CONNECTION_POOLING_FIELD = 'enableConnectionPooling';

const useStyles = makeStyles((theme) => ({
  subText: {
    fontSize: '11.5px',
    lineHeight: '18px',
    fontWeight: 400,
    color: '#4E5F6D'
  }
}));

export const ConnectionPoolingField: FC<ConnectionPoolFieldProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<DatabaseSettingsProps>();
  const { t } = useTranslation();
  const classes = useStyles();

  //watchers
  const isYSQLEnabled = useWatch({ name: YSQL_FIELD });
  //   const dbVersionValue = useWatch({ name: SOFTWARE_VERSION_FIELD });

  const isConnectionPoolSupported = true;

  useUpdateEffect(() => {
    if (!isYSQLEnabled) setValue(CONNECTION_POOLING_FIELD, false);
  }, [isYSQLEnabled]);

  //   useUpdateEffect(() => {
  //     //set toggle to false if unsupported db version is selected
  //     if (!isVersionConnectionPoolSupported(dbVersionValue))
  //       setValue(CONNECTION_POOLING_FIELD, false);
  //   }, [dbVersionValue]);

  return (
    <Box
      sx={{
        display: 'flex',
        width: '548px',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px',
        padding: '16px 24px'
      }}
      data-testid="ConnectionPoolingField-Container"
    >
      <YBTooltip
        title={
          isYSQLEnabled ? (
            isConnectionPoolSupported ? (
              ''
            ) : (
              <Typography className={classes.subText}>
                {t('universeForm.advancedConfig.conPoolVersionTooltip')}
              </Typography>
            )
          ) : (
            <Typography className={classes.subText}>
              {t('universeForm.advancedConfig.conPoolYSQLWarn')}
            </Typography>
          )
        }
      >
        <div>
          <YBToggleField
            name={CONNECTION_POOLING_FIELD}
            inputProps={{
              'data-testid': 'PGCompatibiltyField-Toggle'
            }}
            control={control}
            disabled={disabled || !isYSQLEnabled || !isConnectionPoolSupported}
          />
        </div>
      </YBTooltip>
      <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
          <Typography variant="body2" sx={{ color: '#0B1117', ml: 1, mr: 1 }}>
            {t('universeForm.advancedConfig.enableConnectionPooling')}&nbsp;
          </Typography>
          <YBEarlyAccessTag />
        </Box>

        <Box sx={{ ml: 1 }}>
          <Typography className={classes.subText}>
            <Trans>{t('universeForm.advancedConfig.conPoolTooltip')}</Trans>
          </Typography>
        </Box>
      </Box>
    </Box>
  );
};
