import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBToggleField, YBLabel, YBTooltip, YBEarlyAccessTag } from '../../../../../../components';
import { isVersionConnectionPoolSupported } from '../../../utils/helpers';
import { UniverseFormData } from '../../../utils/dto';
import {
  CONNECTION_POOLING_FIELD,
  YSQL_FIELD,
  SOFTWARE_VERSION_FIELD
} from '../../../utils/constants';
//icons
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface ConnectionPoolFieldProps {
  disabled: boolean;
}

const useStyles = makeStyles((theme) => ({
  subText: {
    fontSize: '11.5px',
    lineHeight: '16px',
    fontWeight: 400,
    color: '#67666C'
  }
}));

export const ConnectionPoolingField: FC<ConnectionPoolFieldProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useStyles();

  //watchers
  const isYSQLEnabled = useWatch({ name: YSQL_FIELD });
  const dbVersionValue = useWatch({ name: SOFTWARE_VERSION_FIELD });

  const isConnectionPoolSupported = isVersionConnectionPoolSupported(dbVersionValue);

  useUpdateEffect(() => {
    if (!isYSQLEnabled) setValue(CONNECTION_POOLING_FIELD, false);
  }, [isYSQLEnabled]);

  useUpdateEffect(() => {
    //set toggle to false if unsupported db version is selected
    if (!isVersionConnectionPoolSupported(dbVersionValue))
      setValue(CONNECTION_POOLING_FIELD, false);
  }, [dbVersionValue]);

  return (
    <Box display="flex" width="100%" data-testid="ConnectionPoolingField-Container">
      <YBTooltip
        interactive={true}
        title={
          <Typography className={classes.subText}>
            {isYSQLEnabled
              ? isConnectionPoolSupported
                ? ''
                : t('universeForm.advancedConfig.conPoolVersionTooltip')
              : t('universeForm.advancedConfig.conPoolYSQLWarn')}
          </Typography>
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
      <Box display={'flex'} flexDirection={'row'}>
        <YBLabel dataTestId="ConnectionPoolingField-Label" width="300px">
          {t('universeForm.advancedConfig.enableConnectionPooling')} &nbsp;
          <YBTooltip title={t('universeForm.advancedConfig.conPoolTooltip')}>
            <img alt="Info" src={InfoMessageIcon} />
          </YBTooltip>
          &nbsp;&nbsp;
          <YBEarlyAccessTag />
        </YBLabel>
      </Box>
    </Box>
  );
};
