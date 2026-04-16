import { useState } from "react";
import clsx from "clsx";
import { Popover, Typography, makeStyles } from "@material-ui/core";

const YBPopoverStyles = makeStyles((theme) => ({
  popover: {
    pointerEvents: 'none'
  },
  root: {
    padding: theme.spacing(1),
    color: '#67666C'
  },
  minWidth: {
    width: '210px'
  }
}));


export const YBPopover = ({ children, hoverMsg, minWidth = false }: { children: JSX.Element; hoverMsg: string | JSX.Element, minWidth?: boolean }) => {
  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null);

  const classes = YBPopoverStyles();

  const handlePopoverOpen = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };

  const handlePopoverClose = () => {
      setAnchorEl(null);
  };

  const open = Boolean(anchorEl);

  return (
    <div onMouseEnter={handlePopoverOpen} onMouseLeave={handlePopoverClose}>
      {children}
      <Popover
        id="dependent-perm-disabled"
        className={classes.popover}
        classes={{
          paper: clsx(classes.root, minWidth && classes.minWidth)
        }}
        open={open}
        anchorEl={anchorEl}
        anchorOrigin={{
          vertical: 'top',
          horizontal: 'left'
        }}
        transformOrigin={{
          vertical: 'bottom',
          horizontal: 'left'
        }}
        onClose={handlePopoverClose}
        disableRestoreFocus
      >
        <Typography variant="subtitle1">{hoverMsg}</Typography>
      </Popover>
    </div>
  );
};
