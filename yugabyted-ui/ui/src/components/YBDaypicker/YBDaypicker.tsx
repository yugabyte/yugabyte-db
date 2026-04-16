import { Box, Button, ButtonGroup, FormHelperText, makeStyles, Theme, FormLabel } from '@material-ui/core';
import clsx from 'clsx';
import React, { useState, FC, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { getDefaultWeekDays } from '@app/helpers';

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    padding: theme.spacing(1, 2),
    fontSize: 13,
    textTransform: 'inherit',
    '&.MuiButtonGroup-groupedContainedPrimary:not(:last-child)': {
      borderColor: theme.palette.grey[200]
    },
    backgroundColor: theme.palette.primary[200],
    color: theme.palette.primary[600],
    '&:hover': {
      backgroundColor: theme.palette.primary[300]
    }
  },
  selected: {
    backgroundColor: theme.palette.primary.main,
    color: theme.palette.common.white,
    '&:hover': {
      backgroundColor: theme.palette.primary[700]
    }
  },
  unselected: {
    backgroundColor: theme.palette.grey[200],
    color: theme.palette.grey[400],
    '&:hover': {
      backgroundColor: theme.palette.grey[300]
    }
  }
}));

export interface YBDaypickerProps {
  onChange?: (value: number[]) => void;
  value?: number[];
  error?: boolean;
  label?: string;
  helperText?: string;
}

export interface WeekDay {
  name: string;
  value: number;
  selected: boolean;
}

export const YBDaypicker: FC<YBDaypickerProps> = ({ onChange, value, error, label, helperText }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const weekDays: WeekDay[] = getDefaultWeekDays(t);
  const valueSet: Set<number> = new Set(value);
  weekDays.forEach((day) => {
    if (valueSet.has(day.value)) {
      day.selected = true;
    }
  });
  const [days, setDays] = useState(weekDays);

  const updateSelection = (day: WeekDay) => {
    const newWeeksDays: WeekDay[] = [...(days ?? [])];

    //keep atleast one day selected - START
    const selectedDays: WeekDay[] = newWeeksDays.filter((d) => d.selected);
    if (selectedDays.length === 1 && selectedDays[0].name === day.name) return false;
    //keep atleast one day selected - END

    newWeeksDays.map((d) => {
      if (day.name === d.name) {
        d.selected = !d.selected;
        if (d.selected) {
          valueSet.add(d.value);
        } else {
          valueSet.delete(d.value);
        }
      }
    });
    setDays(newWeeksDays);

    if (onChange) {
      onChange(Array.from(valueSet) ?? []);
    }
    return true;
  };

  useEffect(() => {
    setDays(weekDays);
  }, [value, setDays]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <Box>
      {label && (
        <Box mb={0.5}>
          <FormLabel component="legend">{label}</FormLabel>
        </Box>
      )}
      <ButtonGroup disableElevation variant="contained" color="primary">
        {days?.map((wDay: WeekDay) => {
          return (
            <Button
              onClick={() => updateSelection(wDay)}
              className={clsx(classes.root, wDay.selected ? classes.selected : classes.unselected)}
              key={wDay.name}
            >
              {wDay.name}
            </Button>
          );
        })}
      </ButtonGroup>
      {error && <FormHelperText error>{helperText}</FormHelperText>}
    </Box>
  );
};
