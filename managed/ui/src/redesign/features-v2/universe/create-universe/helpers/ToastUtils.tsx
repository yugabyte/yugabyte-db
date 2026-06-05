import {
  createContext,
  FC,
  ReactNode,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState
} from 'react';
import { createPortal } from 'react-dom';
import { YBAlert, AlertVariant } from '@yugabyte-ui-library/core';

import { ToastNotificationDuration } from '@app/redesign/helpers/constants';
import { ErrorBoundary } from '@app/redesign/features/rbac/common/validator/ErrorBoundary';

export type YBToastApi = {
  info: (text: string) => void;
  success: (text: string) => void;
  error: (text: string) => void;
  warning: (text: string) => void;
  inProgress: (text: string) => void;
};

type ToastEntry = { id: string; text: string; variant: AlertVariant };

const ToastPortalContext = createContext<YBToastApi | null>(null);

let toastSeq = 0;
const nextToastId = () => `yb-toast-${++toastSeq}`;

const toastApiRef: { current: YBToastApi | null } = { current: null };

const TOAST_AUTO_DISMISS_MS = ToastNotificationDuration.DEFAULT;

const TOAST_VERTICAL_GAP_PX = 58;
const TOAST_TOP_OFFSET_PX = 24;

const YBToastLayer: FC<{
  item: ToastEntry;
  index: number;
  dismiss: (id: string) => void;
}> = ({ item, index, dismiss }) => {
  const onDismiss = useCallback(() => dismiss(item.id), [dismiss, item.id]);

  useEffect(() => {
    const timer = window.setTimeout(onDismiss, TOAST_AUTO_DISMISS_MS);
    return () => window.clearTimeout(timer);
  }, [item.id, onDismiss]);

  return (
    <div
      style={{
        position: 'fixed',
        top: TOAST_TOP_OFFSET_PX + index * TOAST_VERTICAL_GAP_PX,
        left: '50%',
        transform: 'translateX(-50%)',
        zIndex: 10000 + index,
        maxWidth: 'min(560px, calc(100vw - 32px))',
        pointerEvents: 'auto'
      }}
    >
      <YBAlert
        open
        text={item.text?.toString() ?? ''}
        variant={item.variant}
        isToast={false}
        onClose={onDismiss}
      />
    </div>
  );
};

export const YBToastProvider: FC<{ children: ReactNode }> = ({ children }) => {
  const [toasts, setToasts] = useState<ToastEntry[]>([]);
  const dismiss = useCallback((id: string) => {
    setToasts((prev) => prev.filter((t) => t.id !== id));
  }, []);

  const api = useMemo<YBToastApi>(
    () => ({
      info: (text: string) =>
        setToasts((prev) => [...prev, { id: nextToastId(), text, variant: AlertVariant.Info }]),
      success: (text: string) =>
        setToasts((prev) => [...prev, { id: nextToastId(), text, variant: AlertVariant.Success }]),
      error: (text: string) =>
        setToasts((prev) => [...prev, { id: nextToastId(), text, variant: AlertVariant.Error }]),
      warning: (text: string) =>
        setToasts((prev) => [...prev, { id: nextToastId(), text, variant: AlertVariant.Warning }]),
      inProgress: (text: string) =>
        setToasts((prev) => [
          ...prev,
          { id: nextToastId(), text, variant: AlertVariant.InProgress }
        ])
    }),
    []
  );

  useEffect(() => {
    toastApiRef.current = api;
    return () => {
      toastApiRef.current = null;
    };
  }, [api]);

  const portal =
    typeof document !== 'undefined'
      ? createPortal(
          <div aria-live="polite" aria-relevant="additions text">
            {toasts.map((item, index) => (
              <YBToastLayer key={item.id} item={item} index={index} dismiss={dismiss} />
            ))}
          </div>,
          document.body
        )
      : null;

  return (
    <ToastPortalContext.Provider value={api}>
      <ErrorBoundary>
        {children}
        {portal}
      </ErrorBoundary>
    </ToastPortalContext.Provider>
  );
};

export const useYBToast = (): YBToastApi => {
  const ctx = useContext(ToastPortalContext);
  if (!ctx) {
    throw new Error('useYBToast must be used within YBToastProvider');
  }
  return ctx;
};

/** Imperative API; only works after {@link YBToastProvider} has mounted. */
export const YBToast: YBToastApi = {
  info: (text: string) => toastApiRef.current?.info(text),
  success: (text: string) => toastApiRef.current?.success(text),
  error: (text: string) => toastApiRef.current?.error(text),
  warning: (text: string) => toastApiRef.current?.warning(text),
  inProgress: (text: string) => toastApiRef.current?.inProgress(text)
};
