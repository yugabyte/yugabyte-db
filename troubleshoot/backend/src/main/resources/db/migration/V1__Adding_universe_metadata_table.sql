CREATE
    TABLE
        universe_metadata(
            id uuid NOT NULL,
            api_token VARCHAR(255) NOT NULL,
            platform_url TEXT NOT NULL,
            metrics_url TEXT NOT NULL,
            CONSTRAINT pk_performance_recommendation PRIMARY KEY(id)
        );
