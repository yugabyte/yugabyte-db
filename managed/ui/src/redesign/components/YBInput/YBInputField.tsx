import { ReactElement } from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { YBInput, YBInputProps } from './YBInput';

export type YBInputFieldProps<T extends FieldValues> = UseControllerProps<T> & YBInputProps;

export const YBInputField = <T extends FieldValues>(props: YBInputFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybInputProps } = props;
  const {
    field: { ref, ...fieldProps },
    fieldState
  } = useController({ name, rules, defaultValue, control, shouldUnregister });
  const { inputProps } = ybInputProps;
  return (
    <YBInput
      {...fieldProps}
      {...ybInputProps}
      inputProps={{
        'data-testid': `YBInputField-${name}`,
        ...inputProps
      }}
      inputRef={ref}
      error={!!fieldState.error}
      helperText={fieldState.error?.message ?? ybInputProps.helperText}
    />
  );
};
