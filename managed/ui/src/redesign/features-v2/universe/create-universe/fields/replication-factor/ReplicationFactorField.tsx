import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { YBFormControlLabel, YBButtonGroup } from '@yugabyte-ui-library/core';
import { ResilienceAndRegionsProps } from '../../steps/resilence-regions/dtos';
import { REPLICATION_FACTOR } from '../FieldNames';

interface ReplicationFactorFieldProps {
  hideLabel?: boolean;
  replication_options?: string[];
  fieldName?: string;
  /** When true, segmented buttons are non-interactive (e.g. fault tolerance None). */
  segmentDisabled?: boolean;
}

export const ReplicationFactorField = ({
  hideLabel = false,
  replication_options,
  fieldName = REPLICATION_FACTOR,
  segmentDisabled = false
}: ReplicationFactorFieldProps) => {
  const { setValue, watch } = useFormContext<ResilienceAndRegionsProps>();

  const replicationFactorVal = watch(fieldName as keyof ResilienceAndRegionsProps);

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
              setValue(fieldName as keyof ResilienceAndRegionsProps, parseInt(options), {
                shouldValidate: true
              });
              setReplicationFactor(options);
            },
            buttonProps: {
              disabled: segmentDisabled
            }
          }))}
          value={replicationFactorVal + ''}
          dataTestId="replication-factor-field"
          size="large"
        />
      }
      sx={{ alignItems: 'flex-start', gap: '4px' }}
      label={hideLabel ? null : <span>{t('replicationFactor')}</span>}
    />
  );
};
