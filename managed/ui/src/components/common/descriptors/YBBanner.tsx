import { cloneElement, ReactElement, ReactNode } from 'react';
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

  // The className prop in bannerIcon will be overridden
  bannerIcon?: ReactElement;
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
    defaultBannerIcon = <InfoOutlined className={clsx(styles.icon, iconClassName)} />;
  } else if (variant === YBBannerVariant.WARNING) {
    variantClassName = styles.warning;
    defaultBannerIcon = <i className={clsx('fa fa-warning', styles.icon, iconClassName)} />;
  } else if (variant === YBBannerVariant.DANGER) {
    variantClassName = styles.danger;
    defaultBannerIcon = <i className={clsx('fa fa-warning', styles.icon, iconClassName)} />;
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
      {showBannerIcon &&
        (bannerIcon
          ? cloneElement(bannerIcon, { className: clsx(styles.icon, iconClassName) })
          : defaultBannerIcon)}
      <div className={styles.childrenContainer}>{children}</div>
    </div>
  );
};
