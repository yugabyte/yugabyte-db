import { useState } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBFormControlLabel, YBButtonGroup } from '@yugabyte-ui-library/core';
import { REPLICATION_FACTOR } from '../FieldNames';
import { ResilienceAndRegionsProps } from '../../steps/resilence-regions/dtos';

interface ReplicationFactorFieldProps {
  hideLabel?: boolean;
  replication_options?: string[];
}

export const ReplicationFactorField = ({
  hideLabel = false,
  replication_options
}: ReplicationFactorFieldProps) => {
  const { setValue, watch } = useFormContext<ResilienceAndRegionsProps>();

  const replicationFactorVal = watch(REPLICATION_FACTOR);

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions'
  });
  const [replicationFactor, setReplicationFactor] = useState<string>(replicationFactorVal + '');

  const REPLICATION_OPTIONS = replication_options ?? ['1', '3', '5', '7'];

  return (
    <YBFormControlLabel
      labelPlacement="top"
      control={
        <YBButtonGroup
          buttons={REPLICATION_OPTIONS.map((options) => ({
            label: options,
            value: options,
            onClick: () => {
              setValue(REPLICATION_FACTOR, parseInt(options));
              setReplicationFactor(options);
            }
          }))}
          active={replicationFactor + ''}
          dataTestId="replication-factor-field"
        />
      }
      sx={{ alignItems: 'flex-start', gap: '4px' }}
      label={hideLabel ? null : <span>{t('replicationFactor')}</span>}
    />
  );
};
