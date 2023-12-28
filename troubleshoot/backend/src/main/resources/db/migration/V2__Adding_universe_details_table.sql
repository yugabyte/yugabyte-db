CREATE
    TABLE
        universe_details(
            universe_uuid uuid NOT NULL,
            name text NOT NULL,
            universe_details JSONB NOT NULL,
            CONSTRAINT pk_universe_details PRIMARY KEY(universe_uuid)
        );
