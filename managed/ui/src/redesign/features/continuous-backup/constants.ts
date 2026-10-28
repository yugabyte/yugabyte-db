import { TimeUnitType } from '../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ReactSelectOption } from '../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';

/**
 * Standard input field width for all continuous backup input text fields and dropdowns.
 */
export const INPUT_FIELD_WIDTH_PX = 550;

/**
 * Form unit options for backup frequency. Labels come from `common.duration` i18n keys.
 * Only seconds and minutes are supported in the configure form.
 */
export type BackupFrequencyFormUnit = Extract<TimeUnitType, 'SECONDS' | 'MINUTES'>;

export const BACKUP_FREQUENCY_FORM_UNITS: readonly BackupFrequencyFormUnit[] = [
  'SECONDS',
  'MINUTES'
];

export const BACKUP_FREQUENCY_UNIT_TO_DURATION_I18N_KEY = {
  SECONDS: 'seconds',
  MINUTES: 'minutes'
} satisfies Record<BackupFrequencyFormUnit, 'seconds' | 'minutes'>;

export const getBackupFrequencyUnitOptions = (
  getDurationLabel: (unit: BackupFrequencyFormUnit) => string
): ReactSelectOption[] =>
  BACKUP_FREQUENCY_FORM_UNITS.map((unit) => ({
    value: unit,
    label: getDurationLabel(unit)
  }));
