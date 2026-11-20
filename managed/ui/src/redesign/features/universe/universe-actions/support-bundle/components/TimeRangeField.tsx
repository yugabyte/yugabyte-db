import { useTranslation } from 'react-i18next';
import { mui, YBDatePicker, YBSelect } from '@yugabyte-ui-library/core';

import { TimeRangePreset, TimeRangeValue } from '../model/formState';

const { Box, MenuItem } = mui;

interface TimeRangeFieldProps {
  label: string;
  value: TimeRangeValue;
  onChange: (value: TimeRangeValue) => void;
  presets: TimeRangePreset[];
  dataTestId: string;
}

export const TimeRangeField = ({
  label,
  value,
  onChange,
  presets,
  dataTestId
}: TimeRangeFieldProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.create.timeRange'
  });
  const isCustom = value.preset === TimeRangePreset.CUSTOM;
  const isRangeInvalid =
    isCustom && !!value.customStart && !!value.customEnd && value.customStart >= value.customEnd;

  return (
    <Box display="flex" flexDirection="column" gap={1.5}>
      <Box width={275}>
        <YBSelect
          label={label}
          value={value.preset}
          onChange={(event) =>
            onChange({ ...value, preset: event.target.value as TimeRangePreset })
          }
          fullWidth
          dataTestId={dataTestId}
        >
          {presets.map((preset) => (
            <MenuItem key={preset} value={preset}>
              {t(`preset.${preset}`)}
            </MenuItem>
          ))}
        </YBSelect>
      </Box>
      {isCustom && (
        <Box display="flex" gap={2}>
          <YBDatePicker
            label={t('start')}
            value={value.customStart}
            onChange={(date) => onChange({ ...value, customStart: date })}
            calendar={{ showTimeSelector: true }}
            dataTestid={`${dataTestId}-Start`}
          />
          <YBDatePicker
            label={t('end')}
            value={value.customEnd}
            onChange={(date) => onChange({ ...value, customEnd: date })}
            calendar={{ showTimeSelector: true }}
            error={isRangeInvalid}
            helperText={isRangeInvalid ? t('endBeforeStart') : undefined}
            dataTestid={`${dataTestId}-End`}
          />
        </Box>
      )}
    </Box>
  );
};
