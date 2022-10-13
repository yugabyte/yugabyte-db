import React, { ReactNode } from 'react';
import clsx from 'clsx';

import styles from './stylesheets/YBBanner.module.scss';

interface YBBannerProps {
  children: ReactNode;

  bannerIcon?: ReactNode;
  className?: string;
  showBannerIcon?: boolean;
  variant?: YBBannerVariant;
}

// Add more variants as needed
export enum YBBannerVariant {
  WARNING = 'warning',
  DANGER = 'danger'
}

export const YBBanner = ({
  className,
  children,
  variant,
  bannerIcon,
  showBannerIcon = true
}: YBBannerProps) => {
  let variantClassName = '';
  let defaultBannerIcon = null;

  if (variant === YBBannerVariant.WARNING) {
    variantClassName = styles.warning;
    defaultBannerIcon = <i className="fa fa-warning" />;
  } else if (variant === YBBannerVariant.DANGER) {
    variantClassName = styles.danger;
    defaultBannerIcon = <i className="fa fa-warning" />;
  }

  return (
    <div className={clsx(styles.bannerContainer, className, variantClassName)}>
      {showBannerIcon && <div className={styles.icon}>{bannerIcon ?? defaultBannerIcon}</div>}
      <div className={styles.childrenContainer}>{children}</div>
    </div>
  );
};
