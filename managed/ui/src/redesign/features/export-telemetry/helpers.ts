import { UniverseItem } from '@app/components/configRedesign/providerRedesign/providerView/providerDetails/UniverseTable';
import { Universe } from '@app/redesign/helpers/dtos';
import _ from 'lodash';

export const getLinkedUniverses = (logUUID: string, universes: Universe[]) =>
  universes.reduce((linkedUniverses: UniverseItem[], universe) => {
    const linkedClusters = universe.universeDetails.clusters.filter(
      (cluster) =>
        _.get(cluster, 'userIntent.auditLogConfig.universeLogsExporterConfig[0].exporterUuid') ===
        logUUID
    );
    if (!_.isEmpty(linkedClusters)) {
      linkedUniverses.push({
        ...universe,
        linkedClusters: linkedClusters
      });
    }
    return linkedUniverses;
  }, []);
