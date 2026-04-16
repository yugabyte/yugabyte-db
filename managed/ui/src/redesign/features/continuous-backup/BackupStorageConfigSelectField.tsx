import { Box, FormHelperText } from '@material-ui/core';
import { FieldValues, useController, UseControllerProps, UseFormReset } from 'react-hook-form';
import {
  BackupStorageConfigReactSelectOption,
  BackupStorageConfigSelect,
  BackupStorageConfigSelectProps
} from './BackupStorageConfigSelect';

export interface BackupStorageConfigSelectFieldProps<TFieldValues extends FieldValues> {
  useControllerProps: UseControllerProps<TFieldValues>;
  reset: UseFormReset<TFieldValues>;

  defaultStorageConfigUuid?: string;
  storageSelectProps?: BackupStorageConfigSelectProps;
  width?: string; // Will override dynamic width.
  autoSizeMinWidth?: number; // If specified, will grow the field width from a given minimum.
  accessoryContainerWidthPx?: number;
  maxWidth?: string;
}

export const BackupStorageConfigSelectField = <TFieldValues extends FieldValues>({
  width,
  maxWidth,
  autoSizeMinWidth,
  accessoryContainerWidthPx = 0,
  defaultStorageConfigUuid,
  reset,
  useControllerProps,
  storageSelectProps
}: BackupStorageConfigSelectFieldProps<TFieldValues>) => {
  const { field, fieldState } = useController(useControllerProps);

  const handleOnOptionsLoaded = (options: BackupStorageConfigReactSelectOption[]) => {
    if (defaultStorageConfigUuid) {
      const defaultOption = options.find((option) => option.value === defaultStorageConfigUuid);
      reset({ [useControllerProps.name]: defaultOption } as TFieldValues);
    }
  };

  // We scale the width by multiplying the option label length by a constant factor and add a constant
  // width to account for accessory components like pills/badges.
  const autoSizedWidth = Math.max(
    (field.value?.label?.length ?? 0) * 11 + accessoryContainerWidthPx,
    autoSizeMinWidth ?? 300
  );
  return (
    <Box width={width ?? (autoSizeMinWidth ? `${autoSizedWidth}px` : '100%')} maxWidth={maxWidth}>
      <BackupStorageConfigSelect
        {...storageSelectProps}
        {...field}
        onOptionsLoaded={handleOnOptionsLoaded}
        autoSizeMinWidth={autoSizeMinWidth}
      />
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </Box>
  );
};
