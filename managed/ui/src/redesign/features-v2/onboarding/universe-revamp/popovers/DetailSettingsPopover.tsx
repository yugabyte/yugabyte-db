import {
  FC,
  forwardRef,
  RefObject,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
  useState
} from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';

const { Popper } = mui;

/** Distance below the Settings tab anchor. */
const POPOVER_OFFSET: [number, number] = [0, 12];

export const DETAIL_SETTINGS_POPOVER_DISMISS_KEY = 'yb_detail_settings_popover_dismissed';
const DETAIL_SETTINGS_POPOVER_OPEN_EVENT = 'yb-detail-settings-popover-open';

interface DetailSettingsPopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  onClose: () => void;
}

export interface SettingsTabTitleWithPopoverHandle {
  /**
   * Opens the tip when it has not been dismissed yet.
   * @returns true when navigation should be blocked.
   */
  tryIntercept: () => boolean;
}

export const isDetailSettingsPopoverDismissed = (): boolean =>
  localStorage.getItem(DETAIL_SETTINGS_POPOVER_DISMISS_KEY) === 'true';

export const dismissDetailSettingsPopover = (): void => {
  localStorage.setItem(DETAIL_SETTINGS_POPOVER_DISMISS_KEY, 'true');
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
  onClose
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.detailSettingsPopover'
  });

  return (
    <Popper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.Bottom}
      modifiers={[
        {
          name: 'offset',
          options: {
            offset: POPOVER_OFFSET
          }
        }
      ]}
      sx={{ zIndex: 2200 }}
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
    </Popper>
  );
};

/**
 * Settings tab label. Exposes {@link SettingsTabTitleWithPopoverHandle.tryIntercept}
 * so the tab panel can block navigation until the tip is dismissed.
 */
export const SettingsTabTitleWithPopover = forwardRef<SettingsTabTitleWithPopoverHandle>(
  function SettingsTabTitleWithPopover(_props, ref) {
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

    useImperativeHandle(
      ref,
      () => ({
        tryIntercept: () => {
          if (isDetailSettingsPopoverDismissed()) {
            return false;
          }
          setOpen(true);
          return true;
        }
      }),
      []
    );

    return (
      <>
        <span ref={anchorRef}>Settings</span>
        <DetailSettingsPopover open={open} anchorRef={anchorRef} onClose={handleClose} />
      </>
    );
  }
);
