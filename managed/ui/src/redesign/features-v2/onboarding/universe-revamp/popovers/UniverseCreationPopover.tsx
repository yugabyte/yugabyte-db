import { FC, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import {
  TourStep,
  dismissTourStep,
  isTourStepDismissed,
  subscribeTourProgressReady
} from '../tour-progress';
import { OnboardingTourPopper } from './OnboardingTourPopper';

/** Skidding shifts the left-placed popover downward along the anchor. */
const POPOVER_OFFSET: [number, number] = [30, 0];

const UNIVERSE_CREATION_POPOVER_OPEN_EVENT = 'yb-universe-creation-popover-open';

interface UniverseCreationPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  /** Permanent Hide Tip. */
  onClose: () => void;
  /** Transient click-away close. */
  onClickAway: () => void;
}

export const isUniverseCreationPopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.UnivCreate);

export const dismissUniverseCreationPopover = (): void => {
  dismissTourStep(TourStep.UnivCreate);
};

/** Opens the Create Universe tip when it has not been dismissed yet. */
export const requestOpenUniverseCreationPopover = (): void => {
  if (isUniverseCreationPopoverDismissed()) {
    return;
  }
  window.dispatchEvent(new CustomEvent(UNIVERSE_CREATION_POPOVER_OPEN_EVENT));
};

export const useUniverseCreationPopover = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    const handleOpenRequest = () => {
      if (!isUniverseCreationPopoverDismissed()) {
        setOpen(true);
      }
    };
    window.addEventListener(UNIVERSE_CREATION_POPOVER_OPEN_EVENT, handleOpenRequest);

    // After tip already permanently dismissed → show Create tip on its own on load.
    const tryAutoOpen = () => {
      if (isTourStepDismissed(TourStep.AfterExp) && !isUniverseCreationPopoverDismissed()) {
        setOpen(true);
      }
    };
    const unsub = subscribeTourProgressReady(tryAutoOpen);

    return () => {
      window.removeEventListener(UNIVERSE_CREATION_POPOVER_OPEN_EVENT, handleOpenRequest);
      unsub();
    };
  }, []);

  const handleClose = useCallback(() => {
    dismissUniverseCreationPopover();
    setOpen(false);
  }, []);

  return {
    open,
    anchorRef,
    handleClose,
    handleClickAway: handleClose
  };
};

export const UniverseCreationPopover: FC<UniverseCreationPopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.universeCreationPopover'
  });

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.Left}
      offset={POPOVER_OFFSET}
      zIndex={(theme) => theme.zIndex.modal}
      onClickAway={onClickAway}
    >
      <YBTourSpotlight
        title={t('title')}
        body={t('body')}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.LeftStart}
        dataTestId="universe-creation-popover-spotlight"
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};
