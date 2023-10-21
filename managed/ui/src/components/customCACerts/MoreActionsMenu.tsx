/*
 * Created on Tue Jun 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { Component, FC, ReactComponentElement } from 'react';
import { Divider, Menu, MenuItem, makeStyles } from '@material-ui/core';

type MoreOptionsProps = {
  children: React.ReactElement;
  menuOptions: {
    icon?: React.ReactNode;
    text: string;
    callback: Function;
    className?: string;
    isDivider?: boolean;
    disabled?: boolean;
    menuItemWrapper?: (elem: JSX.Element) => JSX.Element;
    "data-testid"?: string;
  }[];
};

const useStyles = makeStyles((theme) => ({
  menuStyles: {
    '& .MuiMenuItem-root': {
      height: '50px',
      minWidth: '190px'
    },
    '& .MuiMenuItem-root svg': {
      marginRight: theme.spacing(1.4),
      color: theme.palette.orange[500],
      height: '20px',
      width: '20px'
    }
  }
}));

export const MoreActionsMenu: FC<MoreOptionsProps> = ({ children, menuOptions }) => {
  const [anchorEl, setAnchorEl] = React.useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };
  const reactChild = React.cloneElement(children, {
    onClick: (e: any) => {
      handleClick(e);
      children.props.onClick?.(e);
    }
  });
  const classes = useStyles();
  return (
    <>
      {reactChild}
      <Menu
        id="ca-cert-more-menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        getContentAnchorEl={null}
        className={classes.menuStyles}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'right'
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'right'
        }}
      >
        {menuOptions.map((m) => {
          const menuItem = (
            <MenuItem
              key={m.text}
              data-testid={m['data-testid'] ?? `ca-cert-${m.text}`}
              className={m.className}
              onClick={() => {
                handleClose();
                m.callback();
              }}
              disabled={m.disabled ?? false}
            >
              {m.icon && m.icon}
              {m.text}
            </MenuItem>
          );
          return m.isDivider ? (<Divider key={m.text} />) : m.menuItemWrapper ? m.menuItemWrapper(menuItem) : menuItem;
        }
        )}
      </Menu>
    </>
  );
};
