import { ReactElement } from 'react';
import { UseControllerProps, useController, FieldValues } from 'react-hook-form';
import { YBPassword } from './YBPassword';
import type { YBInputProps } from '../YBInput/YBInput';

type YBPasswordFieldProps<T extends FieldValues> = UseControllerProps<T> &
  YBInputProps & { hidePasswordButton?: boolean };

export const YBPasswordField = <T extends FieldValues>(
  props: YBPasswordFieldProps<T>
): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybInputProps } = props;

  const {
    field: { ref, ...fieldProps },
    fieldState
  } = useController({ name, rules, defaultValue, control, shouldUnregister });
  return (
    <YBPassword
      {...fieldProps}
      {...ybInputProps}
      inputRef={ref}
      error={!!fieldState.error}
      helperText={fieldState.error?.message}
      {...props}
    />
  );
};
