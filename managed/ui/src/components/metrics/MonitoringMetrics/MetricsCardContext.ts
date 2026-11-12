import { createContext, ReactNode } from 'react';

export const MetricsCardActionsContext = createContext<ReactNode>(null);

// Loader GraphTab shows instead of YBLoading; null keeps the default.
export const MetricsCardLoaderContext = createContext<ReactNode>(null);
