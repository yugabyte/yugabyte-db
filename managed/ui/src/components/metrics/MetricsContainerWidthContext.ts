import { createContext } from 'react';

// Chart width for a view narrower than a universe tab; null keeps MetricsPanel's default.
export const MetricsContainerWidthContext = createContext<number | null>(null);
