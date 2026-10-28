import { FC, RefObject, useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { TourStep, dismissTourStep, isTourStepDismissed, subscribeTourProgressReady } from '../tour-progress';
import { OnboardingTourPopper } from './OnboardingTourPopper';

/** Gap between the dropdown trigger and the left-placed tip. */
const POPOVER_DISTANCE = 8;

/**
 * Distance from the card's top edge to the arrow center for a `-start` placement
 * (22px arrow inset + half of the 12px arrow) — used to line the arrow up with
 * the middle of the trigger button.
 */
const ARROW_CENTER_FROM_CARD_EDGE = 28;

interface AdvancedPlacementPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  /** Permanent Hide Tip. */
  onClose: () => void;
  /** Transient click-away close. */
  onClickAway: () => void;
}

export const isAdvancedPlacementPopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.AdvPlacement);

export const dismissAdvancedPlacementPopover = (): void => {
  dismissTourStep(TourStep.AdvPlacement);
};

export const useAdvancedPlacementPopover = (autoOpen = false) => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    if (!autoOpen) {
      return;
    }
    return subscribeTourProgressReady(() => {
      if (!isAdvancedPlacementPopoverDismissed()) {
        setOpen(true);
      }
    });
  }, [autoOpen]);

  const handleClose = useCallback(() => {
    dismissAdvancedPlacementPopover();
    setOpen(false);
  }, []);

  return {
    open,
    anchorRef,
    handleClose,
    handleClickAway: handleClose
  };
};

export const AdvancedPlacementPopover: FC<AdvancedPlacementPopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.advancedPlacementPopover'
  });

  // Anchor to the trigger button — the wrapper span is as wide as the dropdown menu (340px).
  const dropdownButton =
    (anchorRef.current?.querySelector(
      '[data-testid="edit-placement-actions-button"]'
    ) as HTMLElement | null) ?? anchorRef.current;

  // Keep the tip inside the Edit Universe shell (see data-edit-universe-root).
  const boundaryEl =
    (dropdownButton?.closest('[data-edit-universe-root]') as HTMLElement | null) ?? null;

  // Shift the card up so the top-right arrow lands on the middle of the trigger.
  const anchorHeight = dropdownButton?.offsetHeight ?? 0;
  const skidding = anchorHeight ? Math.round(anchorHeight / 2) - ARROW_CENTER_FROM_CARD_EDGE : 0;

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={dropdownButton}
      placement={TourPlacement.LeftStart}
      offset={[skidding, POPOVER_DISTANCE]}
      boundaryEl={boundaryEl}
      onClickAway={onClickAway}
    >
      <YBTourSpotlight
        title={t('title')}
        body={t('body')}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.LeftStart}
        dataTestId="advanced-placement-popover-spotlight"
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};
