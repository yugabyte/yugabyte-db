/*
 * Created on Fri Oct 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { CSSProperties, cloneElement, useRef, useState } from 'react';
import { useClickAway } from 'react-use';
import { Popover } from '@material-ui/core';
import { RBAC_ERR_MSG_NO_PERM } from './ValidatorUtils';

export const ButtonDisabledPopover = ({
  children,
  popOverOverrides = {}
}: {
  children: React.ReactElement;
  popOverOverrides?: CSSProperties;
}) => {
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);
  const ref = useRef(null);
  const handleClick = (event: React.MouseEvent<HTMLDivElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };

  const onClick = (e: React.MouseEvent<HTMLDivElement>) => {
    e.preventDefault();
    e.stopPropagation();
    handleClick(e);
  };

  const reactChild = cloneElement(children, {
    onClick: onClick
  });

  useClickAway(ref, handleClose);

  return (
    <div ref={ref} onClick={onClick}>
      {reactChild}
      <Popover
        id={'rbac-perm-error'}
        open={open}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'center'
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'center'
        }}
        style={{
          ...popOverOverrides
        }}
      >
        <div
          style={{
            padding: '10px',
            width: '350px',
            zIndex: 1001
          }}
        >
          {RBAC_ERR_MSG_NO_PERM}
        </div>
      </Popover>
    </div>
  );
};
