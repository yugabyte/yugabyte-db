CREATE
    TABLE
        universe_metadata(
            id uuid NOT NULL,
            customer_id uuid NOT NULL,
            api_token VARCHAR(255) NOT NULL,
            platform_url TEXT NOT NULL,
            metrics_url TEXT NOT NULL,
            CONSTRAINT pk_universe_metadata PRIMARY KEY(id)
        );
