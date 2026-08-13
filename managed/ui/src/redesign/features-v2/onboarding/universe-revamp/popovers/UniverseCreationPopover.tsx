import { FC, MouseEvent, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { OnboardingTourPopper } from './OnboardingTourPopper';

/** Skidding shifts the left-placed popover downward along the anchor. */
const POPOVER_OFFSET: [number, number] = [30, 0];

export const UNIVERSE_CREATION_POPOVER_DISMISS_KEY = 'yb_universe_creation_popover_dismissed';
const UNIVERSE_CREATION_POPOVER_OPEN_EVENT = 'yb-universe-creation-popover-open';

interface UniverseCreationPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  onClose: () => void;
}

export const isUniverseCreationPopoverDismissed = (): boolean =>
  localStorage.getItem(UNIVERSE_CREATION_POPOVER_DISMISS_KEY) === 'true';

export const dismissUniverseCreationPopover = (): void => {
  localStorage.setItem(UNIVERSE_CREATION_POPOVER_DISMISS_KEY, 'true');
};

/** Opens the Create Universe tip when it has not been dismissed yet. */
export const requestOpenUniverseCreationPopover = (): void => {
  if (isUniverseCreationPopoverDismissed()) {
    return;
  }
  window.dispatchEvent(new CustomEvent(UNIVERSE_CREATION_POPOVER_OPEN_EVENT));
};

/**
 * Intercepts Create Universe navigation until the tip is dismissed.
 * Returns false when the click was handled (caller should preventDefault).
 */
export const shouldInterceptUniverseCreationClick = (): boolean =>
  !isUniverseCreationPopoverDismissed();

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
    return () => {
      window.removeEventListener(UNIVERSE_CREATION_POPOVER_OPEN_EVENT, handleOpenRequest);
    };
  }, []);

  const handleCreateUniverseClick = useCallback((event: MouseEvent) => {
    if (!shouldInterceptUniverseCreationClick()) {
      return;
    }
    event.preventDefault();
    setOpen(true);
  }, []);

  const handleClose = useCallback(() => {
    dismissUniverseCreationPopover();
    setOpen(false);
  }, []);

  return {
    open,
    anchorRef,
    handleCreateUniverseClick,
    handleClose
  };
};

export const UniverseCreationPopover: FC<UniverseCreationPopoverProps> = ({
  open,
  anchorRef,
  onClose
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
