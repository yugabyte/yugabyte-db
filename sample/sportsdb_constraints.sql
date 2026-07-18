--
-- Name: addresses_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY addresses
    ADD CONSTRAINT addresses_id_key UNIQUE (id);


--
-- Name: affiliation_phases_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY affiliation_phases
    ADD CONSTRAINT affiliation_phases_id_key UNIQUE (id);


--
-- Name: affiliations_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY affiliations
    ADD CONSTRAINT affiliations_id_key UNIQUE (id);


--
-- Name: american_football_action_participants_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_action_participants
    ADD CONSTRAINT american_football_action_participants_id_key UNIQUE (id);


--
-- Name: american_football_action_plays_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_action_plays
    ADD CONSTRAINT american_football_action_plays_id_key UNIQUE (id);


--
-- Name: american_football_defensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_defensive_stats
    ADD CONSTRAINT american_football_defensive_stats_id_key UNIQUE (id);


--
-- Name: american_football_down_progress_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_down_progress_stats
    ADD CONSTRAINT american_football_down_progress_stats_id_key UNIQUE (id);


--
-- Name: american_football_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_event_states
    ADD CONSTRAINT american_football_event_states_id_key UNIQUE (id);


--
-- Name: american_football_fumbles_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_fumbles_stats
    ADD CONSTRAINT american_football_fumbles_stats_id_key UNIQUE (id);


--
-- Name: american_football_offensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_offensive_stats
    ADD CONSTRAINT american_football_offensive_stats_id_key UNIQUE (id);


--
-- Name: american_football_passing_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_passing_stats
    ADD CONSTRAINT american_football_passing_stats_id_key UNIQUE (id);


--
-- Name: american_football_penalties_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_penalties_stats
    ADD CONSTRAINT american_football_penalties_stats_id_key UNIQUE (id);


--
-- Name: american_football_rushing_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_rushing_stats
    ADD CONSTRAINT american_football_rushing_stats_id_key UNIQUE (id);


--
-- Name: american_football_sacks_against_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_sacks_against_stats
    ADD CONSTRAINT american_football_sacks_against_stats_id_key UNIQUE (id);


--
-- Name: american_football_scoring_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_scoring_stats
    ADD CONSTRAINT american_football_scoring_stats_id_key UNIQUE (id);


--
-- Name: american_football_special_teams_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY american_football_special_teams_stats
    ADD CONSTRAINT american_football_special_teams_stats_id_key UNIQUE (id);


--
-- Name: baseball_action_contact_details_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_action_contact_details
    ADD CONSTRAINT baseball_action_contact_details_id_key UNIQUE (id);


--
-- Name: baseball_action_pitches_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_action_pitches
    ADD CONSTRAINT baseball_action_pitches_id_key UNIQUE (id);


--
-- Name: baseball_action_plays_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_action_plays
    ADD CONSTRAINT baseball_action_plays_id_key UNIQUE (id);


--
-- Name: baseball_action_substitutions_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT baseball_action_substitutions_id_key UNIQUE (id);


--
-- Name: baseball_defensive_group_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_defensive_group
    ADD CONSTRAINT baseball_defensive_group_id_key UNIQUE (id);


--
-- Name: baseball_defensive_players_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_defensive_players
    ADD CONSTRAINT baseball_defensive_players_id_key UNIQUE (id);


--
-- Name: baseball_defensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_defensive_stats
    ADD CONSTRAINT baseball_defensive_stats_id_key UNIQUE (id);


--
-- Name: baseball_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT baseball_event_states_id_key UNIQUE (id);


--
-- Name: baseball_offensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_offensive_stats
    ADD CONSTRAINT baseball_offensive_stats_id_key UNIQUE (id);


--
-- Name: baseball_pitching_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY baseball_pitching_stats
    ADD CONSTRAINT baseball_pitching_stats_id_key UNIQUE (id);


--
-- Name: basketball_defensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY basketball_defensive_stats
    ADD CONSTRAINT basketball_defensive_stats_id_key UNIQUE (id);


--
-- Name: basketball_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY basketball_event_states
    ADD CONSTRAINT basketball_event_states_id_key UNIQUE (id);


--
-- Name: basketball_offensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY basketball_offensive_stats
    ADD CONSTRAINT basketball_offensive_stats_id_key UNIQUE (id);


--
-- Name: basketball_rebounding_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY basketball_rebounding_stats
    ADD CONSTRAINT basketball_rebounding_stats_id_key UNIQUE (id);


--
-- Name: basketball_team_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY basketball_team_stats
    ADD CONSTRAINT basketball_team_stats_id_key UNIQUE (id);


--
-- Name: bookmakers_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY bookmakers
    ADD CONSTRAINT bookmakers_id_key UNIQUE (id);


--
-- Name: core_person_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY core_person_stats
    ADD CONSTRAINT core_person_stats_id_key UNIQUE (id);


--
-- Name: core_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY core_stats
    ADD CONSTRAINT core_stats_id_key UNIQUE (id);


--
-- Name: display_names_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY display_names
    ADD CONSTRAINT display_names_id_key UNIQUE (id);


--
-- Name: document_classes_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_classes
    ADD CONSTRAINT document_classes_id_key UNIQUE (id);


--
-- Name: document_contents_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_contents
    ADD CONSTRAINT document_contents_id_key UNIQUE (id);


--
-- Name: document_fixtures_events_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_fixtures_events
    ADD CONSTRAINT document_fixtures_events_id_key UNIQUE (id);


--
-- Name: document_fixtures_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_fixtures
    ADD CONSTRAINT document_fixtures_id_key UNIQUE (id);


--
-- Name: document_package_entry_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_package_entry
    ADD CONSTRAINT document_package_entry_id_key UNIQUE (id);


--
-- Name: document_packages_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY document_packages
    ADD CONSTRAINT document_packages_id_key UNIQUE (id);


--
-- Name: documents_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY documents
    ADD CONSTRAINT documents_id_key UNIQUE (id);


--
-- Name: documents_media_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY documents_media
    ADD CONSTRAINT documents_media_id_key UNIQUE (id);


--
-- Name: events_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY events
    ADD CONSTRAINT events_id_key UNIQUE (id);


--
-- Name: ice_hockey_action_participants_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_action_participants
    ADD CONSTRAINT ice_hockey_action_participants_id_key UNIQUE (id);


--
-- Name: ice_hockey_action_plays_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_action_plays
    ADD CONSTRAINT ice_hockey_action_plays_id_key UNIQUE (id);


--
-- Name: ice_hockey_defensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_defensive_stats
    ADD CONSTRAINT ice_hockey_defensive_stats_id_key UNIQUE (id);


--
-- Name: ice_hockey_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_event_states
    ADD CONSTRAINT ice_hockey_event_states_id_key UNIQUE (id);


--
-- Name: ice_hockey_offensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_offensive_stats
    ADD CONSTRAINT ice_hockey_offensive_stats_id_key UNIQUE (id);


--
-- Name: ice_hockey_player_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY ice_hockey_player_stats
    ADD CONSTRAINT ice_hockey_player_stats_id_key UNIQUE (id);


--
-- Name: injury_phases_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY injury_phases
    ADD CONSTRAINT injury_phases_id_key UNIQUE (id);


--
-- Name: key_aliases_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY key_aliases
    ADD CONSTRAINT key_aliases_id_key UNIQUE (id);


--
-- Name: key_roots_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY key_roots
    ADD CONSTRAINT key_roots_id_key UNIQUE (id);


--
-- Name: latest_revisions_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY latest_revisions
    ADD CONSTRAINT latest_revisions_id_key UNIQUE (id);


--
-- Name: locations_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY locations
    ADD CONSTRAINT locations_id_key UNIQUE (id);


--
-- Name: media_captions_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY media_captions
    ADD CONSTRAINT media_captions_id_key UNIQUE (id);


--
-- Name: media_contents_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY media_contents
    ADD CONSTRAINT media_contents_id_key UNIQUE (id);


--
-- Name: media_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY media
    ADD CONSTRAINT media_id_key UNIQUE (id);


--
-- Name: media_keywords_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY media_keywords
    ADD CONSTRAINT media_keywords_id_key UNIQUE (id);


--
-- Name: motor_racing_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY motor_racing_event_states
    ADD CONSTRAINT motor_racing_event_states_id_key UNIQUE (id);


--
-- Name: motor_racing_qualifying_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY motor_racing_qualifying_stats
    ADD CONSTRAINT motor_racing_qualifying_stats_id_key UNIQUE (id);


--
-- Name: motor_racing_race_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY motor_racing_race_stats
    ADD CONSTRAINT motor_racing_race_stats_id_key UNIQUE (id);


--
-- Name: outcome_totals_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY outcome_totals
    ADD CONSTRAINT outcome_totals_id_key UNIQUE (id);


--
-- Name: participants_events_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY participants_events
    ADD CONSTRAINT participants_events_id_key UNIQUE (id);


--
-- Name: periods_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY periods
    ADD CONSTRAINT periods_id_key UNIQUE (id);


--
-- Name: person_event_metadata_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT person_event_metadata_id_key UNIQUE (id);


--
-- Name: person_phases_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT person_phases_id_key UNIQUE (id);


--
-- Name: persons_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT persons_id_key UNIQUE (id);


--
-- Name: positions_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY positions
    ADD CONSTRAINT positions_id_key UNIQUE (id);


--
-- Name: publishers_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY publishers
    ADD CONSTRAINT publishers_id_key UNIQUE (id);


--
-- Name: roles_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY roles
    ADD CONSTRAINT roles_id_key UNIQUE (id);


--
-- Name: seasons_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY seasons
    ADD CONSTRAINT seasons_id_key UNIQUE (id);


--
-- Name: sites_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY sites
    ADD CONSTRAINT sites_id_key UNIQUE (id);


--
-- Name: soccer_defensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY soccer_defensive_stats
    ADD CONSTRAINT soccer_defensive_stats_id_key UNIQUE (id);


--
-- Name: soccer_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY soccer_event_states
    ADD CONSTRAINT soccer_event_states_id_key UNIQUE (id);


--
-- Name: soccer_foul_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY soccer_foul_stats
    ADD CONSTRAINT soccer_foul_stats_id_key UNIQUE (id);


--
-- Name: soccer_offensive_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY soccer_offensive_stats
    ADD CONSTRAINT soccer_offensive_stats_id_key UNIQUE (id);


--
-- Name: standing_subgroups_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY standing_subgroups
    ADD CONSTRAINT standing_subgroups_id_key UNIQUE (id);


--
-- Name: standings_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY standings
    ADD CONSTRAINT standings_id_key UNIQUE (id);


--
-- Name: stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY stats
    ADD CONSTRAINT stats_id_key UNIQUE (id);


--
-- Name: sub_periods_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY sub_periods
    ADD CONSTRAINT sub_periods_id_key UNIQUE (id);


--
-- Name: sub_seasons_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY sub_seasons
    ADD CONSTRAINT sub_seasons_id_key UNIQUE (id);


--
-- Name: team_american_football_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY team_american_football_stats
    ADD CONSTRAINT team_american_football_stats_id_key UNIQUE (id);


--
-- Name: team_phases_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT team_phases_id_key UNIQUE (id);


--
-- Name: teams_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY teams
    ADD CONSTRAINT teams_id_key UNIQUE (id);


--
-- Name: tennis_action_points_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY tennis_action_points
    ADD CONSTRAINT tennis_action_points_id_key UNIQUE (id);


--
-- Name: tennis_action_volleys_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY tennis_action_volleys
    ADD CONSTRAINT tennis_action_volleys_id_key UNIQUE (id);


--
-- Name: tennis_event_states_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY tennis_event_states
    ADD CONSTRAINT tennis_event_states_id_key UNIQUE (id);


--
-- Name: tennis_return_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY tennis_return_stats
    ADD CONSTRAINT tennis_return_stats_id_key UNIQUE (id);


--
-- Name: tennis_service_stats_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY tennis_service_stats
    ADD CONSTRAINT tennis_service_stats_id_key UNIQUE (id);


--
-- Name: wagering_moneylines_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY wagering_moneylines
    ADD CONSTRAINT wagering_moneylines_id_key UNIQUE (id);


--
-- Name: wagering_odds_lines_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY wagering_odds_lines
    ADD CONSTRAINT wagering_odds_lines_id_key UNIQUE (id);


--
-- Name: wagering_runlines_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY wagering_runlines
ADD CONSTRAINT wagering_runlines_id_key UNIQUE (id);


--
-- Name: wagering_straight_spread_lines_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY wagering_straight_spread_lines
ADD CONSTRAINT wagering_straight_spread_lines_id_key UNIQUE (id);


--
-- Name: wagering_total_score_lines_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY wagering_total_score_lines
ADD CONSTRAINT wagering_total_score_lines_id_key UNIQUE (id);


--
-- Name: weather_conditions_id_key; Type: CONSTRAINT; Schema: public; Owner: postgres81; Tablespace: 
--

ALTER TABLE ONLY weather_conditions
ADD CONSTRAINT weather_conditions_id_key UNIQUE (id);

