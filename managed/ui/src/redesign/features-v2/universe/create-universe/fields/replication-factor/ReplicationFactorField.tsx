import { type ReactNode, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, type FieldValues } from 'react-hook-form';
import { YBFormControlLabel, YBButtonGroup } from '@yugabyte-ui-library/core';
import { REPLICATION_FACTOR } from '../FieldNames';

interface ReplicationFactorFieldProps {
  hideLabel?: boolean;
  replication_options?: string[];
  fieldName?: string;
  /** When true, segmented buttons are non-interactive (e.g. fault tolerance None). */
  segmentDisabled?: boolean;
  /** When set, replaces the default translated label (still respects hideLabel). */
  label?: ReactNode;
  /** Optional i18n namespace for the default label (when `label` is not passed). */
  labelKeyPrefix?: string;
  /** Optional i18n key for the default label (when `label` is not passed). */
  labelTranslationKey?: string;
  /** Per-option disable (e.g. RF 1 unavailable for multi-region expert). */
  isOptionDisabled?: (option: string) => boolean;
  /** Optional per-option tooltip text (used for disabled options). */
  getOptionTooltip?: (option: string) => string | undefined;
}

export const ReplicationFactorField = ({
  hideLabel = false,
  replication_options,
  fieldName = REPLICATION_FACTOR,
  segmentDisabled = false,
  label: labelOverride,
  labelKeyPrefix = 'createUniverseV2.resilienceAndRegions',
  labelTranslationKey = 'replicationFactor',
  isOptionDisabled,
  getOptionTooltip
}: ReplicationFactorFieldProps) => {
  const { setValue, watch } = useFormContext<FieldValues>();

  const replicationFactorVal = watch(fieldName) as number | undefined;

  const { t } = useTranslation('translation', {
    keyPrefix: labelKeyPrefix
  });
  const [, setReplicationFactor] = useState<string>((replicationFactorVal ?? '') + '');

  const REPLICATION_OPTIONS = replication_options ?? ['1', '3', '5', '7'];

  const defaultLabel = <span>{t(labelTranslationKey)}</span>;

  return (
    <YBFormControlLabel
      labelPlacement="top"
      control={
        <YBButtonGroup
          buttons={REPLICATION_OPTIONS.map((options) => ({
            label: options,
            value: options,
            tooltip: getOptionTooltip?.(options),
            onClick: () => {
              setValue(fieldName, parseInt(options, 10), {
                shouldValidate: true,
                shouldDirty: true
              });
              setReplicationFactor(options);
            },
            buttonProps: {
              disabled: segmentDisabled || (isOptionDisabled?.(options) ?? false),
              dataTestId: `replication-factor-field-option-${options}`
            }
          }))}
          value={(replicationFactorVal ?? '') + ''}
          dataTestId="replication-factor-field"
          size="large"
        />
      }
      sx={{ alignItems: 'flex-start', gap: '4px' }}
      label={hideLabel ? null : (labelOverride ?? defaultLabel)}
    />
  );
};
