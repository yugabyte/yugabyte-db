import React, { FC } from 'react';
import { Menu, PopoverOrigin, MenuProps } from '@material-ui/core';

export interface DropdownProps extends Omit<MenuProps, 'open'> {
  origin: React.ReactElement;
  position?: 'bottom' | 'right';
  growDirection?: 'up' | 'down' | 'left' | 'right';
  keepOpenOnSelect?: boolean;
  className?: string;
  dataTestId?: string;
}

/*
 * A dropdown component that attaches to an anchor element and renders a menu of children.
 * Note, we automatically stop the click event from propagating but sometimes you will want the
 * Menu to remain open, such as for a tooltip-like behavior, in that case pass
 * `keepOpenOnSelect` prop to this component.
 */
export const YBDropdown: FC<DropdownProps> = ({
  children,
  position,
  growDirection,
  origin,
  keepOpenOnSelect,
  className: overrideClassName,
  dataTestId,
  ...menuProps
}) => {
  const [anchorEl, setAnchorEl] = React.useState<HTMLElement | null>(null);

  const handleClick = (event: React.MouseEvent<HTMLDivElement>) => {
    setAnchorEl(event.currentTarget);
    event.stopPropagation();
  };

  const handleClose = (event: React.MouseEvent<HTMLDivElement>) => {
    setAnchorEl(null);
    event.stopPropagation();
  };

  // For closing dropdown after executing onClick of children
  const handleMenuClick = (event: React.MouseEvent<HTMLDivElement>) => {
    if (!keepOpenOnSelect) {
      handleClose(event);
    }
  };

  const open = Boolean(anchorEl);

  // Smart positioning based on user props
  const anchorOrigin = {
    vertical: 'bottom',
    horizontal: 'right'
  } as PopoverOrigin;
  const transformOrigin = {
    vertical: 'top',
    horizontal: 'right'
  } as PopoverOrigin;
  if (position === 'bottom') {
    anchorOrigin.vertical = 'bottom';
    transformOrigin.vertical = 'top';
    if (growDirection === 'right') {
      anchorOrigin.horizontal = 'left';
      transformOrigin.horizontal = 'left';
    } else {
      anchorOrigin.horizontal = 'right';
      transformOrigin.horizontal = 'right';
    }
  } else if (position === 'right') {
    anchorOrigin.horizontal = 'right';
    transformOrigin.horizontal = 'left';
    if (growDirection === 'up') {
      anchorOrigin.vertical = 'bottom';
      transformOrigin.vertical = 'bottom';
    } else {
      anchorOrigin.vertical = 'top';
      transformOrigin.vertical = 'top';
    }
  }

  return (
    <>
      <div
        onClick={handleClick}
        className={overrideClassName}
        style={{ width: 'fit-content' }}
        data-testid={dataTestId}
      >
        {origin}
      </div>
      <Menu
        getContentAnchorEl={null}
        anchorOrigin={anchorOrigin}
        transformOrigin={transformOrigin}
        anchorEl={anchorEl}
        keepMounted
        open={open}
        onClose={handleClose}
        onClick={handleMenuClick}
        {...menuProps}
      >
        {children}
      </Menu>
    </>
  );
};
