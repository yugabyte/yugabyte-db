
export enum ResilienceType {
    REGULAR = 'Regular',
    SINGLE_NODE = 'Single Node',
}

export interface ResilienceAndRegionsProps {
    resilienceType: ResilienceType;
};
