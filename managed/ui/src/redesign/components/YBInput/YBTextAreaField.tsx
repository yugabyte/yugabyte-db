import { FormHelperText, TextareaAutosize, TextareaAutosizeProps } from '@material-ui/core';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';

export type YBTextAreaFieldProps<T extends FieldValues> = UseControllerProps<T> &
  TextareaAutosizeProps;

export const YBTextAreaField = <T extends FieldValues>({
  name,
  rules,
  defaultValue,
  control,
  shouldUnregister,
  ...ybInputProps
}: YBTextAreaFieldProps<T>) => {
  const { field, fieldState } = useController({
    name,
    rules,
    defaultValue,
    control,
    shouldUnregister
  });
  return (
    <>
      <TextareaAutosize
        aria-label="textarea"
        name={field.name}
        value={field.value}
        onChange={field.onChange}
        style={{ width: '100%' }}
        {...ybInputProps}
      />
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </>
  );
};
