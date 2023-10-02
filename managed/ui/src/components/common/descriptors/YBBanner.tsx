import { ReactNode } from 'react';
import clsx from 'clsx';
import { InfoOutlined } from '@material-ui/icons';

import styles from './stylesheets/YBBanner.module.scss';

// Add more variants as needed
export enum YBBannerVariant {
  INFO = 'info',
  WARNING = 'warning',
  DANGER = 'danger'
}

interface YBBannerProps {
  children: ReactNode;

  bannerIcon?: ReactNode;
  className?: string;
  iconClassName?: string;
  // feature banner is used to distinguish the top level banners primarily
  // used for communicating feature state/information,
  isFeatureBanner?: boolean;
  showBannerIcon?: boolean;
  variant?: YBBannerVariant;
}

export const YBBanner = ({
  className,
  children,
  variant,
  bannerIcon,
  iconClassName,
  showBannerIcon = true,
  isFeatureBanner = false
}: YBBannerProps) => {
  let variantClassName = '';
  let defaultBannerIcon = null;

  if (variant === YBBannerVariant.INFO) {
    variantClassName = styles.info;
    defaultBannerIcon = <InfoOutlined />;
  } else if (variant === YBBannerVariant.WARNING) {
    variantClassName = styles.warning;
    defaultBannerIcon = <i className="fa fa-warning" />;
  } else if (variant === YBBannerVariant.DANGER) {
    variantClassName = styles.danger;
    defaultBannerIcon = <i className="fa fa-warning" />;
  }

  return (
    <div
      className={clsx(
        styles.bannerContainer,
        variantClassName,
        isFeatureBanner && styles.featureBanner,
        className
      )}
    >
      {showBannerIcon && (
        <div className={clsx(styles.icon, iconClassName)}>{bannerIcon ?? defaultBannerIcon}</div>
      )}
      <div className={styles.childrenContainer}>{children}</div>
    </div>
  );
};
