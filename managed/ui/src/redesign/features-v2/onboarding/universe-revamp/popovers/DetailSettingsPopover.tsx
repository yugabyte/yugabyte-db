import { FC, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { TourStep, dismissTourStep, isTourStepDismissed } from '../tour-progress';
import { OnboardingTourPopper } from './OnboardingTourPopper';

/** Distance below the Settings tab anchor. */
const POPOVER_OFFSET: [number, number] = [0, 12];

const DETAIL_SETTINGS_POPOVER_OPEN_EVENT = 'yb-detail-settings-popover-open';

interface DetailSettingsPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  /** Permanent Hide Tip. */
  onClose: () => void;
  /** Transient click-away close. */
  onClickAway: () => void;
}

export const isDetailSettingsPopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.DetailSettings);

export const dismissDetailSettingsPopover = (): void => {
  dismissTourStep(TourStep.DetailSettings);
};

/** Opens the Settings tip when it has not been dismissed yet. */
export const requestOpenDetailSettingsPopover = (): void => {
  if (isDetailSettingsPopoverDismissed()) {
    return;
  }
  window.dispatchEvent(new CustomEvent(DETAIL_SETTINGS_POPOVER_OPEN_EVENT));
};

export const DetailSettingsPopover: FC<DetailSettingsPopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.detailSettingsPopover'
  });

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.Bottom}
      offset={POPOVER_OFFSET}
      onClickAway={onClickAway}
    >
      <YBTourSpotlight
        title={t('title')}
        body={t('body')}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.Bottom}
        dataTestId="detail-settings-popover-spotlight"
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};

/** Settings tab label with onboarding tip anchored to the tab title. */
export const SettingsTabTitleWithPopover: FC = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    const handleOpenRequest = () => {
      if (!isDetailSettingsPopoverDismissed()) {
        setOpen(true);
      }
    };
    window.addEventListener(DETAIL_SETTINGS_POPOVER_OPEN_EVENT, handleOpenRequest);
    return () => {
      window.removeEventListener(DETAIL_SETTINGS_POPOVER_OPEN_EVENT, handleOpenRequest);
    };
  }, []);

  const handleClose = useCallback(() => {
    dismissDetailSettingsPopover();
    setOpen(false);
  }, []);

  return (
    <>
      <span ref={anchorRef}>Settings</span>
      <DetailSettingsPopover
        open={open}
        anchorRef={anchorRef}
        onClose={handleClose}
        onClickAway={handleClose}
      />
    </>
  );
};
