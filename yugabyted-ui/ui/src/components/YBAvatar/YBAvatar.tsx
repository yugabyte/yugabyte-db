import React, { FC } from 'react';
import { AccountCircle } from '@material-ui/icons';
import { makeStyles } from '@material-ui/core';

export interface AvatarProps {
  size?: 'small' | 'medium' | 'large';
  imageUrl?: string;
}

const useStyles = makeStyles((theme) => ({
  smallIcon: {
    width: theme.spacing(3),
    height: theme.spacing(3),
    borderRadius: '50%'
  },
  mediumIcon: {
    width: theme.spacing(4),
    height: theme.spacing(4),
    borderRadius: '50%'
  },
  largeIcon: {
    width: theme.spacing(7),
    height: theme.spacing(7),
    borderRadius: '50%'
  }
}));

export const YBAvatar: FC<AvatarProps> = ({ size = 'medium', imageUrl }) => {
  const classes = useStyles();
  let iconClass = classes.mediumIcon;
  if (size === 'small') {
    iconClass = classes.smallIcon;
  } else if (size === 'large') {
    iconClass = classes.largeIcon;
  }

  if (!imageUrl) {
    return <AccountCircle className={iconClass} />;
  }

  // TODO: Add resizing of image to better fit container
  return <img src={imageUrl} className={iconClass} />;
};
