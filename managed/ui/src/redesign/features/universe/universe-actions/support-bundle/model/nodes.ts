export interface SupportBundleNode {
  nodeName: string | null;
  cloudInfo?: { region?: string };
}

export interface RegionNodeGroup {
  region: string;
  nodeNames: string[];
}

/** Groups named nodes by region for the Nodes picker, both sorted by name. */
export const groupNodesByRegion = (nodes: SupportBundleNode[] | undefined): RegionNodeGroup[] => {
  const byRegion = new Map<string, string[]>();
  (nodes ?? []).forEach((node) => {
    if (!node.nodeName) {
      return;
    }
    const region = node.cloudInfo?.region ?? '';
    byRegion.set(region, [...(byRegion.get(region) ?? []), node.nodeName]);
  });
  return Array.from(byRegion.entries())
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([region, nodeNames]) => ({
      region,
      nodeNames: nodeNames.sort((a, b) => a.localeCompare(b, undefined, { numeric: true }))
    }));
};
