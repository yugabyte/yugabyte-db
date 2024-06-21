import { UniverseItem } from './types';
import { Universe } from '../../universe/universe-form/utils/dto';
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
