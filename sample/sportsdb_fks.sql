--
-- Name: fk_add_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY addresses
    ADD CONSTRAINT fk_add_loc_id__loc_id FOREIGN KEY (location_id) REFERENCES locations(id);


--
-- Name: fk_aff_doc_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_documents
    ADD CONSTRAINT fk_aff_doc_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_aff_doc_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_documents
    ADD CONSTRAINT fk_aff_doc_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_aff_eve_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_events
    ADD CONSTRAINT fk_aff_eve_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_aff_eve_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_events
    ADD CONSTRAINT fk_aff_eve_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_aff_med_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_media
    ADD CONSTRAINT fk_aff_med_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_aff_med_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations_media
    ADD CONSTRAINT fk_aff_med_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_aff_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliations
    ADD CONSTRAINT fk_aff_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_affiliations_affiliation_phases; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliation_phases
    ADD CONSTRAINT fk_affiliations_affiliation_phases FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_affiliations_affiliation_phases1; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliation_phases
    ADD CONSTRAINT fk_affiliations_affiliation_phases1 FOREIGN KEY (ancestor_affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_ame_foo_act_par_ame_foo_act_pla_id__ame_foo_act_pla_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY american_football_action_participants
    ADD CONSTRAINT fk_ame_foo_act_par_ame_foo_act_pla_id__ame_foo_act_pla_id FOREIGN KEY (american_football_action_play_id) REFERENCES american_football_action_plays(id);


--
-- Name: fk_ame_foo_act_par_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY american_football_action_participants
    ADD CONSTRAINT fk_ame_foo_act_par_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_ame_foo_act_pla_ame_foo_eve_sta_id__ame_foo_eve_sta_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY american_football_action_plays
    ADD CONSTRAINT fk_ame_foo_act_pla_ame_foo_eve_sta_id__ame_foo_eve_sta_id FOREIGN KEY (american_football_event_state_id) REFERENCES american_football_event_states(id);


--
-- Name: fk_ame_foo_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY american_football_event_states
    ADD CONSTRAINT fk_ame_foo_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_ame_foo_eve_sta_tea_in_pos_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY american_football_event_states
    ADD CONSTRAINT fk_ame_foo_eve_sta_tea_in_pos_id__tea_id FOREIGN KEY (team_in_possession_id) REFERENCES teams(id);


--
-- Name: fk_bas_act_con_det_bas_act_pit_id__bas_act_pit_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_contact_details
    ADD CONSTRAINT fk_bas_act_con_det_bas_act_pit_id__bas_act_pit_id FOREIGN KEY (baseball_action_pitch_id) REFERENCES baseball_action_pitches(id);


--
-- Name: fk_bas_act_pit_bas_def_gro_id__bas_def_gro_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_pitches
    ADD CONSTRAINT fk_bas_act_pit_bas_def_gro_id__bas_def_gro_id FOREIGN KEY (baseball_defensive_group_id) REFERENCES baseball_defensive_group(id);


--
-- Name: fk_bas_act_pla_bas_eve_sta_id__bas_eve_sta_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_plays
    ADD CONSTRAINT fk_bas_act_pla_bas_eve_sta_id__bas_eve_sta_id FOREIGN KEY (baseball_event_state_id) REFERENCES baseball_event_states(id);


--
-- Name: fk_bas_act_sub_bas_eve_sta_id__bas_eve_sta_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT fk_bas_act_sub_bas_eve_sta_id__bas_eve_sta_id FOREIGN KEY (baseball_event_state_id) REFERENCES baseball_event_states(id);


--
-- Name: fk_bas_act_sub_per_ori_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT fk_bas_act_sub_per_ori_id__per_id FOREIGN KEY (person_original_id) REFERENCES persons(id);


--
-- Name: fk_bas_act_sub_per_ori_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT fk_bas_act_sub_per_ori_pos_id__pos_id FOREIGN KEY (person_original_position_id) REFERENCES positions(id);


--
-- Name: fk_bas_act_sub_per_rep_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT fk_bas_act_sub_per_rep_id__per_id FOREIGN KEY (person_replacing_id) REFERENCES persons(id);


--
-- Name: fk_bas_act_sub_per_rep_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_substitutions
    ADD CONSTRAINT fk_bas_act_sub_per_rep_pos_id__pos_id FOREIGN KEY (person_replacing_position_id) REFERENCES positions(id);


--
-- Name: fk_bas_def_pla_bas_def_gro_id__bas_def_gro_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_defensive_players
    ADD CONSTRAINT fk_bas_def_pla_bas_def_gro_id__bas_def_gro_id FOREIGN KEY (baseball_defensive_group_id) REFERENCES baseball_defensive_group(id);


--
-- Name: fk_bas_def_pla_pla_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_defensive_players
    ADD CONSTRAINT fk_bas_def_pla_pla_id__per_id FOREIGN KEY (player_id) REFERENCES persons(id);


--
-- Name: fk_bas_def_pla_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_defensive_players
    ADD CONSTRAINT fk_bas_def_pla_pos_id__pos_id FOREIGN KEY (position_id) REFERENCES positions(id);


--
-- Name: fk_bas_eve_sta_bat_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_bat_id__per_id FOREIGN KEY (batter_id) REFERENCES persons(id);


--
-- Name: fk_bas_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_bas_eve_sta_pit_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_pit_id__per_id FOREIGN KEY (pitcher_id) REFERENCES persons(id);


--
-- Name: fk_bas_eve_sta_run_on_fir_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_run_on_fir_id__per_id FOREIGN KEY (runner_on_first_id) REFERENCES persons(id);


--
-- Name: fk_bas_eve_sta_run_on_sec_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_run_on_sec_id__per_id FOREIGN KEY (runner_on_second_id) REFERENCES persons(id);


--
-- Name: fk_bas_eve_sta_run_on_thi_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_event_states
    ADD CONSTRAINT fk_bas_eve_sta_run_on_thi_id__per_id FOREIGN KEY (runner_on_third_id) REFERENCES persons(id);


--
-- Name: fk_baseball_action_plays_baseball_action_pitches; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY baseball_action_pitches
    ADD CONSTRAINT fk_baseball_action_plays_baseball_action_pitches FOREIGN KEY (baseball_action_play_id) REFERENCES baseball_action_plays(id);


--
-- Name: fk_bask_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY basketball_event_states
    ADD CONSTRAINT fk_bask_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_boo_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY bookmakers
    ADD CONSTRAINT fk_boo_loc_id__loc_id FOREIGN KEY (location_id) REFERENCES locations(id);


--
-- Name: fk_boo_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY bookmakers
    ADD CONSTRAINT fk_boo_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_cor_per_sta_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY core_person_stats
    ADD CONSTRAINT fk_cor_per_sta_pos_id__pos_id FOREIGN KEY (position_id) REFERENCES positions(id);


--
-- Name: fk_doc_con_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_contents
    ADD CONSTRAINT fk_doc_con_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_doc_doc_fix_id__doc_fix_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents
    ADD CONSTRAINT fk_doc_doc_fix_id__doc_fix_id FOREIGN KEY (document_fixture_id) REFERENCES document_fixtures(id);


--
-- Name: fk_doc_fix_doc_cla_id__doc_cla_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_fixtures
    ADD CONSTRAINT fk_doc_fix_doc_cla_id__doc_cla_id FOREIGN KEY (document_class_id) REFERENCES document_classes(id);


--
-- Name: fk_doc_fix_eve_doc_fix_id__doc_fix_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_fixtures_events
    ADD CONSTRAINT fk_doc_fix_eve_doc_fix_id__doc_fix_id FOREIGN KEY (document_fixture_id) REFERENCES document_fixtures(id);


--
-- Name: fk_doc_fix_eve_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_fixtures_events
    ADD CONSTRAINT fk_doc_fix_eve_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_doc_fix_eve_lat_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_fixtures_events
    ADD CONSTRAINT fk_doc_fix_eve_lat_doc_id__doc_id FOREIGN KEY (latest_document_id) REFERENCES documents(id);


--
-- Name: fk_doc_fix_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_fixtures
    ADD CONSTRAINT fk_doc_fix_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_doc_med_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents_media
    ADD CONSTRAINT fk_doc_med_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_doc_med_med_cap_id__med_cap_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents_media
    ADD CONSTRAINT fk_doc_med_med_cap_id__med_cap_id FOREIGN KEY (media_caption_id) REFERENCES media_captions(id);


--
-- Name: fk_doc_med_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents_media
    ADD CONSTRAINT fk_doc_med_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_doc_pac_ent_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_package_entry
    ADD CONSTRAINT fk_doc_pac_ent_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_doc_pac_ent_doc_pac_id__doc_pac_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY document_package_entry
    ADD CONSTRAINT fk_doc_pac_ent_doc_pac_id__doc_pac_id FOREIGN KEY (document_package_id) REFERENCES document_packages(id);


--
-- Name: fk_doc_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents
    ADD CONSTRAINT fk_doc_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_doc_sou_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY documents
    ADD CONSTRAINT fk_doc_sou_id__pub_id FOREIGN KEY (source_id) REFERENCES publishers(id);


--
-- Name: fk_eve_doc_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_documents
    ADD CONSTRAINT fk_eve_doc_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_eve_doc_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_documents
    ADD CONSTRAINT fk_eve_doc_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_eve_med_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_media
    ADD CONSTRAINT fk_eve_med_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_eve_med_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_media
    ADD CONSTRAINT fk_eve_med_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_eve_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events
    ADD CONSTRAINT fk_eve_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_eve_sit_id__sit_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events
    ADD CONSTRAINT fk_eve_sit_id__sit_id FOREIGN KEY (site_id) REFERENCES sites(id);


--
-- Name: fk_eve_sub_sea_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_sub_seasons
    ADD CONSTRAINT fk_eve_sub_sea_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_eve_sub_sea_sub_sea_id__sub_sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY events_sub_seasons
    ADD CONSTRAINT fk_eve_sub_sea_sub_sea_id__sub_sea_id FOREIGN KEY (sub_season_id) REFERENCES sub_seasons(id);


--
-- Name: fk_ice_hoc_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY ice_hockey_event_states
    ADD CONSTRAINT fk_ice_hoc_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_inj_pha_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY injury_phases
    ADD CONSTRAINT fk_inj_pha_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_inj_pha_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY injury_phases
    ADD CONSTRAINT fk_inj_pha_sea_id__sea_id FOREIGN KEY (season_id) REFERENCES seasons(id);


--
-- Name: fk_key_roots_key_aliases; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY key_aliases
    ADD CONSTRAINT fk_key_roots_key_aliases FOREIGN KEY (key_root_id) REFERENCES key_roots(id);


--
-- Name: fk_lat_rev_lat_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY latest_revisions
    ADD CONSTRAINT fk_lat_rev_lat_doc_id__doc_id FOREIGN KEY (latest_document_id) REFERENCES documents(id);


--
-- Name: fk_med_cap_cap_aut_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media_captions
    ADD CONSTRAINT fk_med_cap_cap_aut_id__per_id FOREIGN KEY (caption_author_id) REFERENCES persons(id);


--
-- Name: fk_med_cap_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media_captions
    ADD CONSTRAINT fk_med_cap_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_med_con_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media_contents
    ADD CONSTRAINT fk_med_con_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_med_cre_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media
    ADD CONSTRAINT fk_med_cre_id__per_id FOREIGN KEY (credit_id) REFERENCES persons(id);


--
-- Name: fk_med_cre_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media
    ADD CONSTRAINT fk_med_cre_loc_id__loc_id FOREIGN KEY (creation_location_id) REFERENCES locations(id);


--
-- Name: fk_med_key_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media_keywords
    ADD CONSTRAINT fk_med_key_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_med_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY media
    ADD CONSTRAINT fk_med_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_mot_rac_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY motor_racing_event_states
    ADD CONSTRAINT fk_mot_rac_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_out_tot_sta_sub_id__sta_sub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY outcome_totals
    ADD CONSTRAINT fk_out_tot_sta_sub_id__sta_sub_id FOREIGN KEY (standing_subgroup_id) REFERENCES standing_subgroups(id);


--
-- Name: fk_par_eve_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY participants_events
    ADD CONSTRAINT fk_par_eve_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_per_bir_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT fk_per_bir_loc_id__loc_id FOREIGN KEY (birth_location_id) REFERENCES locations(id);


--
-- Name: fk_per_dea_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT fk_per_dea_loc_id__loc_id FOREIGN KEY (death_location_id) REFERENCES locations(id);


--
-- Name: fk_per_doc_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons_documents
    ADD CONSTRAINT fk_per_doc_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_per_doc_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons_documents
    ADD CONSTRAINT fk_per_doc_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_per_eve_met_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT fk_per_eve_met_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_per_eve_met_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT fk_per_eve_met_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_per_eve_met_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT fk_per_eve_met_pos_id__pos_id FOREIGN KEY (position_id) REFERENCES positions(id);


--
-- Name: fk_per_eve_met_rol_id__rol_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT fk_per_eve_met_rol_id__rol_id FOREIGN KEY (role_id) REFERENCES roles(id);


--
-- Name: fk_per_eve_met_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_event_metadata
    ADD CONSTRAINT fk_per_eve_met_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_per_hom_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT fk_per_hom_loc_id__loc_id FOREIGN KEY (hometown_location_id) REFERENCES locations(id);


--
-- Name: fk_per_med_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons_media
    ADD CONSTRAINT fk_per_med_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_per_med_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons_media
    ADD CONSTRAINT fk_per_med_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_per_par_eve_id__par_eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY periods
    ADD CONSTRAINT fk_per_par_eve_id__par_eve_id FOREIGN KEY (participant_event_id) REFERENCES participants_events(id);


--
-- Name: fk_per_pha_end_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT fk_per_pha_end_sea_id__sea_id FOREIGN KEY (end_season_id) REFERENCES seasons(id);


--
-- Name: fk_per_pha_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT fk_per_pha_per_id__per_id FOREIGN KEY (person_id) REFERENCES persons(id);


--
-- Name: fk_per_pha_reg_pos_id__pos_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT fk_per_pha_reg_pos_id__pos_id FOREIGN KEY (regular_position_id) REFERENCES positions(id);


--
-- Name: fk_per_pha_rol_id__rol_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT fk_per_pha_rol_id__rol_id FOREIGN KEY (role_id) REFERENCES roles(id);


--
-- Name: fk_per_pha_sta_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY person_phases
    ADD CONSTRAINT fk_per_pha_sta_sea_id__sea_id FOREIGN KEY (start_season_id) REFERENCES seasons(id);


--
-- Name: fk_per_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT fk_per_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_per_res_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY persons
    ADD CONSTRAINT fk_per_res_loc_id__loc_id FOREIGN KEY (residence_location_id) REFERENCES locations(id);


--
-- Name: fk_pos_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY positions
    ADD CONSTRAINT fk_pos_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_sea_lea_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY seasons
    ADD CONSTRAINT fk_sea_lea_id__aff_id FOREIGN KEY (league_id) REFERENCES affiliations(id);


--
-- Name: fk_sea_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY seasons
    ADD CONSTRAINT fk_sea_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_seasons_affiliation_phases; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliation_phases
    ADD CONSTRAINT fk_seasons_affiliation_phases FOREIGN KEY (start_season_id) REFERENCES seasons(id);


--
-- Name: fk_seasons_affiliation_phases1; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY affiliation_phases
    ADD CONSTRAINT fk_seasons_affiliation_phases1 FOREIGN KEY (end_season_id) REFERENCES seasons(id);


--
-- Name: fk_sit_loc_id__loc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY sites
    ADD CONSTRAINT fk_sit_loc_id__loc_id FOREIGN KEY (location_id) REFERENCES locations(id);


--
-- Name: fk_sit_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY sites
    ADD CONSTRAINT fk_sit_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_soc_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY soccer_event_states
    ADD CONSTRAINT fk_soc_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_sta_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY standings
    ADD CONSTRAINT fk_sta_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_sta_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY standings
    ADD CONSTRAINT fk_sta_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_sta_sub_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY standing_subgroups
    ADD CONSTRAINT fk_sta_sub_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_sta_sub_sea_id__sub_sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY standings
    ADD CONSTRAINT fk_sta_sub_sea_id__sub_sea_id FOREIGN KEY (sub_season_id) REFERENCES sub_seasons(id);


--
-- Name: fk_sta_sub_sta_id__sta_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY standing_subgroups
    ADD CONSTRAINT fk_sta_sub_sta_id__sta_id FOREIGN KEY (standing_id) REFERENCES standings(id);


--
-- Name: fk_sub_per_per_id__per_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY sub_periods
    ADD CONSTRAINT fk_sub_per_per_id__per_id FOREIGN KEY (period_id) REFERENCES periods(id);


--
-- Name: fk_sub_sea_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY sub_seasons
    ADD CONSTRAINT fk_sub_sea_sea_id__sea_id FOREIGN KEY (season_id) REFERENCES seasons(id);


--
-- Name: fk_tea_aff_pha_aff_id__aff_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT fk_tea_aff_pha_aff_id__aff_id FOREIGN KEY (affiliation_id) REFERENCES affiliations(id);


--
-- Name: fk_tea_aff_pha_end_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT fk_tea_aff_pha_end_sea_id__sea_id FOREIGN KEY (end_season_id) REFERENCES seasons(id);


--
-- Name: fk_tea_aff_pha_rol_id__rol_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT fk_tea_aff_pha_rol_id__rol_id FOREIGN KEY (role_id) REFERENCES roles(id);


--
-- Name: fk_tea_aff_pha_sta_sea_id__sea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT fk_tea_aff_pha_sta_sea_id__sea_id FOREIGN KEY (start_season_id) REFERENCES seasons(id);


--
-- Name: fk_tea_aff_pha_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY team_phases
    ADD CONSTRAINT fk_tea_aff_pha_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_tea_doc_doc_id__doc_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams_documents
    ADD CONSTRAINT fk_tea_doc_doc_id__doc_id FOREIGN KEY (document_id) REFERENCES documents(id);


--
-- Name: fk_tea_doc_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams_documents
    ADD CONSTRAINT fk_tea_doc_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_tea_hom_sit_id__sit_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams
    ADD CONSTRAINT fk_tea_hom_sit_id__sit_id FOREIGN KEY (home_site_id) REFERENCES sites(id);


--
-- Name: fk_tea_med_med_id__med_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams_media
    ADD CONSTRAINT fk_tea_med_med_id__med_id FOREIGN KEY (media_id) REFERENCES media(id);


--
-- Name: fk_tea_med_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams_media
    ADD CONSTRAINT fk_tea_med_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_tea_pub_id__pub_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY teams
    ADD CONSTRAINT fk_tea_pub_id__pub_id FOREIGN KEY (publisher_id) REFERENCES publishers(id);


--
-- Name: fk_ten_eve_sta_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY tennis_event_states
    ADD CONSTRAINT fk_ten_eve_sta_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_mon_boo_id__boo_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_moneylines
    ADD CONSTRAINT fk_wag_mon_boo_id__boo_id FOREIGN KEY (bookmaker_id) REFERENCES bookmakers(id);


--
-- Name: fk_wag_mon_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_moneylines
    ADD CONSTRAINT fk_wag_mon_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_mon_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_moneylines
    ADD CONSTRAINT fk_wag_mon_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_wag_odd_lin_boo_id__boo_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_odds_lines
    ADD CONSTRAINT fk_wag_odd_lin_boo_id__boo_id FOREIGN KEY (bookmaker_id) REFERENCES bookmakers(id);


--
-- Name: fk_wag_odd_lin_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_odds_lines
    ADD CONSTRAINT fk_wag_odd_lin_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_odd_lin_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_odds_lines
    ADD CONSTRAINT fk_wag_odd_lin_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_wag_run_boo_id__boo_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_runlines
    ADD CONSTRAINT fk_wag_run_boo_id__boo_id FOREIGN KEY (bookmaker_id) REFERENCES bookmakers(id);


--
-- Name: fk_wag_run_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_runlines
    ADD CONSTRAINT fk_wag_run_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_run_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_runlines
    ADD CONSTRAINT fk_wag_run_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_wag_str_spr_lin_boo_id__boo_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_straight_spread_lines
    ADD CONSTRAINT fk_wag_str_spr_lin_boo_id__boo_id FOREIGN KEY (bookmaker_id) REFERENCES bookmakers(id);


--
-- Name: fk_wag_str_spr_lin_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_straight_spread_lines
    ADD CONSTRAINT fk_wag_str_spr_lin_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_str_spr_lin_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_straight_spread_lines
    ADD CONSTRAINT fk_wag_str_spr_lin_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_wag_tot_sco_lin_boo_id__boo_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_total_score_lines
    ADD CONSTRAINT fk_wag_tot_sco_lin_boo_id__boo_id FOREIGN KEY (bookmaker_id) REFERENCES bookmakers(id);


--
-- Name: fk_wag_tot_sco_lin_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_total_score_lines
    ADD CONSTRAINT fk_wag_tot_sco_lin_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);


--
-- Name: fk_wag_tot_sco_lin_tea_id__tea_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY wagering_total_score_lines
    ADD CONSTRAINT fk_wag_tot_sco_lin_tea_id__tea_id FOREIGN KEY (team_id) REFERENCES teams(id);


--
-- Name: fk_wea_con_eve_id__eve_id; Type: FK CONSTRAINT; Schema: public; Owner: postgres81
--

ALTER TABLE ONLY weather_conditions
    ADD CONSTRAINT fk_wea_con_eve_id__eve_id FOREIGN KEY (event_id) REFERENCES events(id);

