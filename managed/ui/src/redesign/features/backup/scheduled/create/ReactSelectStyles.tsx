/* eslint-disable react/display-name */

/*
 * Created on Tue Aug 13 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import clsx from 'clsx';
import { Styles, components } from 'react-select';
import { makeStyles } from '@material-ui/core';

const useStyles = makeStyles((theme) => ({
  error: {
    color: `${theme.palette.error[500]} !important`,
    borderColor: `${theme.palette.error[500]} !important`,
    backgroundColor: `${theme.palette.error[100]} !important`
  },
  label: {
    fontSize: '13px',
    textTransform: 'capitalize',
    color: theme.palette.ybacolors.labelBackground
  },
  control: {
    borderColor: `${theme.palette.ybacolors.ybBorderGray} !important`,
    boxShadow: 'none !important'
  },
  indicator: {
    color: 'hsl(0,0%,80%) !important',
    opacity: 0.9
  }
}));

export const ReactSelectStyles: Styles = {
  singleValue: (props: any) => ({
    ...props,
    display: 'flex'
  }),
  option: (props, { isFocused }) => ({
    ...props,
    display: 'flex',
    backgroundColor: isFocused ? '#e6edff' : 'white',
    color: '#333'
  }),
  container: (props) => ({
    ...props,
    border: 'none',
    width: '550px'
  }),
  control: (props) => ({
    ...props,
    borderColor: '#E5E5E9 !important',
    boxShadow: 'none !important'
  })
};

export const ReactSelectComponents = (isError: boolean) => {
  const classes = useStyles();
  return {
    ValueContainer: (props: any) => {
      return <components.ValueContainer {...props} className={clsx(isError && classes.error)} />;
    },
    Control: (props: any) => {
      return (
        <components.Control
          {...props}
          className={clsx(classes.control, isError && classes.error)}
        />
      );
    },
    Placeholder: (props: any) => {
      return <components.Placeholder {...props} className={clsx(isError && classes.error)} />;
    },
    IndicatorsContainer: (props: any) => {
      return <components.IndicatorsContainer {...props} className={clsx(classes.indicator)} />;
    }
  };
};
