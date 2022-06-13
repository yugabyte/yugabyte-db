import React, { FC, ReactNode, useState, useContext } from 'react';
import { YBAlert, AlertVariant } from '@app/components';

interface ToastApi {
  addToast: (type: AlertVariant, text: string | ReactNode, autoDismissMs?: number) => void;
}

interface ToastListElement {
  id: number;
  status: AlertVariant;
  text: string | ReactNode;
  timeout?: number;
}

const ToastContext = React.createContext<ToastApi>({
  addToast: () => {
    // Do nothing
  }
});

export const ToastProvider: FC = (props) => {
  const [toastList, setToastList] = useState<ToastListElement[]>([]);

  const addToast = (status: AlertVariant, text: string | ReactNode, autoDismissMs?: number) => {
    const currentTime = Date.now();
    setToastList([
      ...toastList,
      {
        id: currentTime,
        status,
        text,
        timeout: autoDismissMs
      }
    ]);
    if (autoDismissMs) {
      setTimeout(() => {
        removeToast(currentTime);
      }, autoDismissMs);
    }
  };

  const removeToast = (id: number) => {
    const newToastList = toastList.filter((t) => t.id !== id);
    if (newToastList.length !== toastList.length) {
      setToastList(newToastList);
    }
  };

  return (
    <ToastContext.Provider
      value={{
        addToast
      }}
    >
      {props.children}
      {toastList.map((t, index) => (
        <YBAlert
          isToast
          open
          key={`toast-id-${t.id}`}
          text={t.text}
          variant={t.status}
          onClose={() => removeToast(t.id)}
          position={index}
          autoDismiss={t.timeout}
        />
      ))}
    </ToastContext.Provider>
  );
};

export const useToast = (): ToastApi => {
  const toastContext = useContext(ToastContext);

  return {
    addToast: toastContext.addToast
  };
};
