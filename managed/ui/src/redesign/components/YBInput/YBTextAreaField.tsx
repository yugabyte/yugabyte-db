import React from 'react';
import { FormHelperText, TextareaAutosize } from '@material-ui/core';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';

export type YBTextAreaFieldProps<T extends FieldValues> = UseControllerProps<T>;

export const YBTextAreaField = <T extends FieldValues>(
  useControllerProps: YBTextAreaFieldProps<T>
) => {
  const { field, fieldState } = useController(useControllerProps);
  return (
    <>
      <TextareaAutosize
        aria-label="textarea"
        name={field.name}
        value={field.value}
        onChange={field.onChange}
        style={{ width: '100%' }}
      />
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </>
  );
};
