import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { useFormContext, useWatch, Controller } from 'react-hook-form';
import { mui, YBToggleField, YBTooltip, YBInput } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { YBEarlyAccessTag } from '../../../../../components';
import { DEFAULT_COMMUNICATION_PORTS } from '../../helpers/constants';
import { isVersionConnectionPoolSupported } from '../../../../../features/universe/universe-form/utils/helpers';

import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YSQL_FIELD } from '../ysql-settings/YSQLSettingsField';

const { Box, Typography, styled } = mui;

import NextLineIcon from '../../../../../assets/next-line.svg';

interface ConnectionPoolFieldProps {
  disabled: boolean;
  dbVersion: string;
}

const MAX_PORT = 65535;
const CONNECTION_POOLING_FIELD = 'enableConnectionPooling';

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: '11.5px',
  lineHeight: '18px',
  fontWeight: 400,
  color: '#4E5F6D'
}));

export const ConnectionPoolingField: FC<ConnectionPoolFieldProps> = ({ disabled, dbVersion }) => {
  const { control, setValue } = useFormContext<DatabaseSettingsProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.databaseSettings.conPool'
  });

  const CON_POOL_PORTS = [
    {
      id: 'ysqlServerRpcPort',
      label: t('ysqlPortlabel'),
      helperText: (
        <Trans
          i18nKey={'createUniverseV2.databaseSettings.conPool.defaultPortMsg'}
          values={{ port: DEFAULT_COMMUNICATION_PORTS.ysqlServerRpcPort }}
        />
      )
    },
    {
      id: 'internalYsqlServerRpcPort',
      label: t('internalPortLabel'),
      helperText: (
        <Trans
          i18nKey={'createUniverseV2.databaseSettings.conPool.defaultPortMsg'}
          values={{ port: DEFAULT_COMMUNICATION_PORTS.internalYsqlServerRpcPort }}
        />
      )
    }
  ];

  //watchers
  const isYSQLEnabled = useWatch({ name: YSQL_FIELD });
  const isConPoolEnabled = useWatch({ name: CONNECTION_POOLING_FIELD });
  const isOverrideCPEnabled = useWatch({ name: 'overrideCPPorts' });

  const isConnectionPoolSupported = isVersionConnectionPoolSupported(dbVersion);

  useUpdateEffect(() => {
    if (!isYSQLEnabled) setValue(CONNECTION_POOLING_FIELD, false);
  }, [isYSQLEnabled]);

  return (
    <FieldContainer>
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '16px 24px' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '12px' }}>
          <YBTooltip
            title={
              isYSQLEnabled ? (
                isConnectionPoolSupported ? (
                  ''
                ) : (
                  <StyledSubText>{t('versionTooltip')}</StyledSubText>
                )
              ) : (
                <StyledSubText>{t('YSQLWarn')}</StyledSubText>
              )
            }
          >
            <div>
              <YBToggleField
                dataTestId="enable-PG-compatibility-field"
                name={CONNECTION_POOLING_FIELD}
                inputProps={{
                  'data-testid': 'PGCompatibiltyField-Toggle'
                }}
                control={control}
                disabled={disabled || !isYSQLEnabled || !isConnectionPoolSupported}
                label={t('label')}
              />
            </div>
          </YBTooltip>
          <YBEarlyAccessTag />
        </Box>
        <Box sx={{ ml: 5 }}>
          <StyledSubText>
            <Trans>{t('helperMsg')}</Trans>
          </StyledSubText>
        </Box>
      </Box>
      {/* If CP Enabled */}
      {isConPoolEnabled && (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            padding: '16px 24px',
            borderTop: '1px solid #D7DEE4'
          }}
        >
          <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
            <NextLineIcon />
            <Box sx={{ marginBottom: '-5px', mr: 1, ml: 2 }}>
              <YBToggleField
                dataTestId="override-CP-ports-field"
                name={'overrideCPPorts'}
                control={control}
                label={t('overridePorts')}
              />
            </Box>
          </Box>
          {isOverrideCPEnabled && (
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                ml: 5,
                pt: 2,
                pl: 1,
                gap: '16px',
                width: '200px'
              }}
            >
              {CON_POOL_PORTS.map((item) => (
                <Controller
                  name={item.id}
                  render={({ field: { value, onChange } }) => {
                    return (
                      <YBInput
                        value={value}
                        onChange={onChange}
                        label={item.label}
                        helperText={item.helperText}
                        dataTestId={`override-CP-ports-field-${item.id}`}
                        onBlur={(event) => {
                          let port =
                            Number(event.target.value.replace(/\D/g, '')) ||
                            Number(DEFAULT_COMMUNICATION_PORTS[item.id] as string);
                          port = port > MAX_PORT ? MAX_PORT : port;
                          onChange(port);
                        }}
                        // trimWhitespace={false}
                      />
                    );
                  }}
                />
              ))}
            </Box>
          )}
        </Box>
      )}
    </FieldContainer>
  );
};
