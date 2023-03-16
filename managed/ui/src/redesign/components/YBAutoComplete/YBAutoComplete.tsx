import React, { FC, useCallback } from 'react';
import { Autocomplete, AutocompleteProps } from '@material-ui/lab';
import { makeStyles, Theme } from '@material-ui/core';
import Paper from '@material-ui/core/Paper';
import clsx from 'clsx';
import { ReactComponent as CaretDownIcon } from '../../assets/caret-down.svg';
import { ReactComponent as CloseIcon } from '../../assets/close.svg';

import { YBInput, YBInputProps } from '../YBInput/YBInput';

interface YBAutoCompleteInputProps extends YBInputProps {
  'data-testid'?: string;
}
export interface YBAutoCompleteProps
  extends Omit<
    AutocompleteProps<Record<string, string>, boolean, boolean, boolean>,
    'renderInput'
  > {
  getOptionLabel?: (option: Record<string, string>) => string;
  ybInputProps?: YBAutoCompleteInputProps;
  autoCompleteDropdownID?: string;
}

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    '& label.MuiInputLabel-root': {
      marginTop: theme.spacing(0.125)
    },
    '& .MuiAutocomplete-inputRoot': {
      paddingTop: theme.spacing(0.125)
    }
  }
}));

export const YBAutoComplete: FC<YBAutoCompleteProps> = React.forwardRef(
  (
    { ybInputProps, options, getOptionLabel, autoCompleteDropdownID, ...rest }: YBAutoCompleteProps,
    ref
  ) => {
    const classes = useStyles();

    const memoPaperComp = useCallback(
      ({ children }) => {
        return <Paper data-testid={autoCompleteDropdownID}>{children}</Paper>;
      },
      [autoCompleteDropdownID]
    );

    return (
      <Autocomplete
        // style change is needed only for single select mode
        className={clsx(!rest.multiple && classes.root)}
        options={options}
        getOptionLabel={getOptionLabel}
        popupIcon={<CaretDownIcon />}
        closeIcon={<CloseIcon />}
        ChipProps={{
          deleteIcon: <CloseIcon />
        }}
        PaperComponent={memoPaperComp}
        renderInput={(params) => (
          <YBInput
            {...params}
            {...ybInputProps}
            InputProps={{
              ...params.InputProps,
              ...ybInputProps?.InputProps,
              inputRef: ref
            }}
          />
        )}
        {...rest}
      />
    );
  }
);
