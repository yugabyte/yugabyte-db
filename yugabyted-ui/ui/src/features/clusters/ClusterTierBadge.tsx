import React, { FC } from 'react';
import clsx from 'clsx';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { ClusterTier } from '@app/api/src';

export interface TierBadgeProps {
  tier: ClusterTier | string;
}

const useStyles = makeStyles((theme) => ({
  base: {
    fontSize: 10,
    fontWeight: 500,
    lineHeight: 1.2,
    height: theme.spacing(2.5),
    padding: theme.spacing(0.25, 0.75),
    color: theme.palette.grey[900],
    borderRadius: theme.spacing(0.5),
    textTransform: 'uppercase',
    display: 'flex',
    alignItems: 'center'
  },
  free: {
    border: `1px solid ${theme.palette.grey[300]}`,
    backgroundColor: theme.palette.common.white
  },
  paid: {
    backgroundColor: theme.palette.primary[300]
  },
  production: {
    color: theme.palette.common.white,
    backgroundColor: theme.palette.secondary[600]
  },
  platinum: {
    color: theme.palette.common.white,
    backgroundColor: theme.palette.grey[600]
  }
}));

export const showTierBadge = (tier: string): boolean => [ClusterTier.Free, 'PLAT', 'PROD'].includes(tier);

export const ClusterTierBadge: FC<TierBadgeProps> = ({ tier }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  let textContent = '';
  let className = classes.base;

  if (showTierBadge(tier)) {
    switch (tier) {
      case ClusterTier.Free:
        textContent = t('common.free');
        className = clsx(className, classes.free);
        break;
      case 'PLAT':
        textContent = t('common.platinum');
        className = clsx(className, classes.platinum);
        break;
      case 'PROD':
        textContent = t('common.production');
        className = clsx(className, classes.production);
        break;
      default:
        break;
    }

    return <span className={className}>{textContent}</span>;
  }
  return null;
};
