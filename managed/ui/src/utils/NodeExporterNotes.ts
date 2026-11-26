import {
  NODE_EXPORTER_CHECK_METRIC_KEY,
  NODE_EXPORTER_CHECK_REASONS,
  NODE_EXPORTER_COLLECTOR_METRIC_KEY,
  NODE_EXPORTER_PANEL_SOURCES
} from '../components/metrics/constants';
import { getIsKubernetesUniverse } from './UniverseUtils';

interface PanelNote {
  zone: string;
  pod: string;
  text: string;
}

const OVERLAID_REASON_PREFIX = 'overlaid_';

const describeCheck = (reason: string, target?: string): string => {
  if (reason.startsWith(OVERLAID_REASON_PREFIX)) {
    const fsType = reason.slice(OVERLAID_REASON_PREFIX.length);
    return `/proc/diskstats is overlaid (${fsType}), so disk statistics are unavailable`;
  }
  const text = NODE_EXPORTER_CHECK_REASONS[reason] ?? reason;
  return target ? `${text} (${target})` : text;
};

/**
 * Returns one note per pod and failed node_exporter check or collector behind a panel, from the
 * check series queried with the tab. A note means the check failed at some point in the range.
 * Only Kubernetes universes get notes.
 */
export const getNodeExporterPanelNotes = (
  metricKey: string,
  tabMetrics: any,
  universe: any
): string[] => {
  const sources = NODE_EXPORTER_PANEL_SOURCES[metricKey];
  if (!sources || !universe || !getIsKubernetesUniverse(universe)) {
    return [];
  }
  const nodeDetailsSet: any[] = universe.universeDetails?.nodeDetailsSet ?? [];
  const notes: PanelNote[] = [];
  const addNote = (instanceName: string, text: string) => {
    const node = nodeDetailsSet.find((n) => n.nodeName === instanceName);
    const pod = node?.cloudInfo?.kubernetesPodName ?? instanceName;
    const zone = node?.cloudInfo?.az ?? '';
    if (!notes.some((note) => note.pod === pod && note.text === text)) {
      notes.push({ zone, pod, text });
    }
  };

  (tabMetrics?.[NODE_EXPORTER_CHECK_METRIC_KEY]?.data ?? []).forEach((series: any) => {
    const { check, reason, target } = series.labels ?? {};
    // An overlaid /proc/diskstats is reported once, by the diskstats check.
    if (
      !reason ||
      reason === 'ok' ||
      reason === 'diskstats_overlaid' ||
      !sources.checks.includes(check)
    ) {
      return;
    }
    addNote(series.instanceName, describeCheck(reason, target));
  });

  (tabMetrics?.[NODE_EXPORTER_COLLECTOR_METRIC_KEY]?.data ?? []).forEach((series: any) => {
    const collector = series.name;
    if (
      sources.collectors.includes(collector) &&
      (series.y ?? []).some((value: any) => Number(value) === 0)
    ) {
      addNote(series.instanceName, `the ${collector} collector failed`);
    }
  });

  return notes
    .sort((a, b) => a.zone.localeCompare(b.zone) || a.pod.localeCompare(b.pod))
    .map((note) => `${note.pod}${note.zone ? ` (${note.zone})` : ''}: ${note.text}`);
};
