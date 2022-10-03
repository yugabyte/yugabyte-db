import React, { FC, useState, useRef } from 'react';
import clsx from 'clsx';
import { makeStyles, Box, Theme, Paper, Chip, FormHelperText } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import type { FieldError } from 'react-hook-form';
import { throttle } from 'lodash';
import { YBTooltip } from '@app/components';
import { getTextWidth } from '@app/helpers/utils';
import ErrorIcon from '@app/assets/failed-solid.svg';
import CloseIcon from '@app/assets/close.svg';

export interface ValidateRules {
  regexRule: RegExp;
  invalidMsg: string;
}

export interface EntryErrorObj {
  invalid: boolean;
  errorMsg: string;
}
export interface YBMultiEntryProps {
  error?: boolean | FieldError;
  label?: string;
  helperText?: string;
  onChange?: (value: string[]) => void;
  placeholderText?: string;
  value?: string[];
}

const useStyles = makeStyles<Theme, Partial<YBMultiEntryProps>>((theme) => ({
  label: {
    marginBottom: theme.spacing(0.4),
    marginTop: theme.spacing(0.4),
    color: theme.palette.grey[600],
    textTransform: 'uppercase',
    fontSize: '11.5px'
  },
  subLabel: {
    marginTop: theme.spacing(0.4),
    color: theme.palette.grey[600],
    fontSize: '12px'
  },
  entryContainer: {
    padding: theme.spacing(0.5),
    paddingBottom: 0,
    cursor: 'text'
  },
  multiEntryChip: {
    backgroundColor: theme.palette.primary[200],
    borderRadius: 6,
    height: theme.spacing(3),
    maxWidth: `${TextEntryWidth.MAX}px`,
    marginRight: theme.spacing(0.5),
    marginBottom: theme.spacing(0.5),
    '&:hover': {
      backgroundColor: theme.palette.primary[100]
    },
    '& .MuiChip-deleteIcon': {
      width: theme.spacing(2),
      height: theme.spacing(2)
    }
  },
  textEntry: {
    border: 0,
    boxShadow: 'none',
    height: '24px',
    marginBottom: theme.spacing(0.5),
    padding: 0,
    '&:focus': {
      outline: 'none'
    }
  },
  icon: {
    position: 'relative',
    top: '1px'
  },
  error: {
    '& g path:nth-child(2)': {
      fill: theme.palette.error[500]
    }
  }
}));

const enum TextEntryWidth {
  MIN = 150,
  MAX = 548
}

export const YBMultiEntry: FC<YBMultiEntryProps> = (props: YBMultiEntryProps) => {
  const classes = useStyles(props);
  const { t } = useTranslation();
  const { label, onChange, placeholderText, value, error, helperText } = props;
  const errorArr: FieldError[] = Array.isArray(error) ? error : [];
  const [entries, setEntries] = useState(value ?? []);
  const [textEntryStyle, setTextEntryStyle] = useState({
    width: `${TextEntryWidth.MIN}px`
  });
  const textBuffer = 40;
  const textInputRef = useRef<HTMLInputElement>(null);

  const handleDelete = (entry: string) => {
    updateEntries(entries.filter((item) => item !== entry));
  };

  const checkTextEntry = throttle((e: React.KeyboardEvent) => {
    const inputEl = e.target as HTMLInputElement;
    const text = inputEl.value;
    const lastTextEntryChar = text[text.length - 1];
    const textEntryWidth = getTextWidth(text, inputEl);
    const currentTextInputWidth = +textEntryStyle.width.split('px')[0];
    if (textEntryWidth > TextEntryWidth.MIN - textBuffer) {
      if (textEntryWidth > TextEntryWidth.MAX) {
        setTextEntryStyle({
          width: `${TextEntryWidth.MAX}px`
        });
      } else if (textEntryWidth + textBuffer > currentTextInputWidth) {
        const textInputWidth =
          textEntryWidth + textBuffer < TextEntryWidth.MAX ? textEntryWidth + textBuffer : TextEntryWidth.MAX;
        setTextEntryStyle({
          width: `${textInputWidth}px`
        });
      }
    }
    if (lastTextEntryChar === ',' || lastTextEntryChar === ' ' || e.key === 'Enter') {
      const entryArr = text.split(/[ ,]+/).filter((item) => item !== '');
      updateEntries(Array.from(new Set([...entries, ...entryArr])));
      inputEl.value = '';
      setTextEntryStyle({
        width: `${TextEntryWidth.MIN}px`
      });
    }
  }, 200);

  const checkBackspace = throttle((e: React.KeyboardEvent) => {
    const text = (e.target as HTMLInputElement).value;
    if (e.key === 'Backspace' && text.length === 0 && entries.length > 0) {
      updateEntries(entries.slice(0, -1));
    }
  }, 200);

  const handleFieldClick = () => {
    if (textInputRef.current) {
      textInputRef.current.focus();
    }
  };

  const updateEntries = (entryArr: string[]) => {
    setEntries(entryArr);
    if (onChange) onChange(entryArr);
  };

  return (
    <>
      <Box className={classes.label}>{label}</Box>
      <Paper className={classes.entryContainer} onClick={handleFieldClick}>
        {entries.map((entry, index) => {
          const textEntryWidth = getTextWidth(entry, textInputRef.current ?? undefined);
          let tooltipMessage = '';
          const invalid = errorArr[index];
          if (invalid) {
            tooltipMessage = errorArr[index].message ?? '';
          } else if (textEntryWidth > TextEntryWidth.MAX) {
            tooltipMessage = entry;
          }
          return (
            <YBTooltip title={tooltipMessage} placement="top" key={`${entry}-tooltip`}>
              <Chip
                className={classes.multiEntryChip}
                label={entry}
                key={entry}
                icon={
                  invalid ? (
                    <ErrorIcon width={16} height={16} className={clsx(classes.icon, classes.error)} />
                  ) : undefined
                }
                deleteIcon={<CloseIcon className={clsx(classes.icon)} />}
                onDelete={() => {
                  handleDelete(entry);
                }}
              />
            </YBTooltip>
          );
        })}
        <input
          className={classes.textEntry}
          placeholder={placeholderText}
          onKeyUp={(e) => checkTextEntry(e)}
          onKeyDown={(e) => checkBackspace(e)}
          style={textEntryStyle}
          ref={textInputRef}
        />
      </Paper>
      <Box className={classes.subLabel}>{t('common.multiEntryFieldLabel')}</Box>
      {error && textInputRef.current?.value.length === 0 && <FormHelperText error>{helperText}</FormHelperText>}
      {Array.isArray(error) && error.length > 0 && (
        <FormHelperText error>{t('common.removeInvalidEntries')}</FormHelperText>
      )}
      {error && textInputRef.current && textInputRef.current?.value.length > 0 && (
        <FormHelperText error>{t('common.multiEntryHitEnter')}</FormHelperText>
      )}
    </>
  );
};
