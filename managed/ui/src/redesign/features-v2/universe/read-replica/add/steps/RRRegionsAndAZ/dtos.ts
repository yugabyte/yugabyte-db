export type RRPlacementZoneForm = {
  zoneUuid: string | null;
  nodeCount: number;
  dataCopies: number;
};

export type RRPlacementRegionForm = {
  regionUuid: string | null;
  zones: RRPlacementZoneForm[];
  /** True when the user added this region in the current wizard session (not loaded from the universe). */
  isNew?: boolean;
};

export type RRRegionsAndAZSettings = {
  regions: RRPlacementRegionForm[];
};

/** Form root shape (same as persisted `RRRegionsAndAZSettings`). */
export type RRRegionsAndAZFormValues = RRRegionsAndAZSettings;

/** Defaults for a new read-replica placement row (wizard not loaded from an existing async cluster). */
export const DEFAULT_READ_REPLICA_RF = 2;
export const DEFAULT_READ_REPLICA_NODE_COUNT = 3;

export const getEmptyRRPlacementZone = (defaults: {
  nodeCount: number;
  dataCopies: number;
}): RRPlacementZoneForm => ({
  zoneUuid: null,
  nodeCount: defaults.nodeCount,
  dataCopies: defaults.dataCopies
});

export const getEmptyRRPlacementRegion = (options?: { isNew?: boolean }): RRPlacementRegionForm => ({
  regionUuid: null,
  zones: [
    getEmptyRRPlacementZone({
      nodeCount: DEFAULT_READ_REPLICA_NODE_COUNT,
      dataCopies: DEFAULT_READ_REPLICA_RF
    })
  ],
  ...(options?.isNew ? { isNew: true } : {})
});

export const getDefaultRRRegionsAndAZ = (): RRRegionsAndAZSettings => ({
  regions: [getEmptyRRPlacementRegion()]
});
