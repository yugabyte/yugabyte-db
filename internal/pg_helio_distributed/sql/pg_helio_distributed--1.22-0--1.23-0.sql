SET search_path TO helio_distributed;
ALTER TABLE helio_api_distributed.helio_cluster_data ADD PRIMARY KEY (metadata);
RESET search_path;
