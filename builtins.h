
#ifndef ORAFCE_BUILTINS
#define ORAFCE_BUILTINS

/* from aggregate.c */
extern Datum orafce_listagg1_transfn(PG_FUNCTION_ARGS);
extern Datum orafce_listagg2_transfn(PG_FUNCTION_ARGS);
extern Datum orafce_listagg_finalfn(PG_FUNCTION_ARGS);
extern Datum orafce_median4_transfn(PG_FUNCTION_ARGS);
extern Datum orafce_median4_finalfn(PG_FUNCTION_ARGS);
extern Datum orafce_median8_transfn(PG_FUNCTION_ARGS);
extern Datum orafce_median8_finalfn(PG_FUNCTION_ARGS);

/* from alert.c */
extern Datum dbms_alert_register(PG_FUNCTION_ARGS);
extern Datum dbms_alert_remove(PG_FUNCTION_ARGS);
extern Datum dbms_alert_removeall(PG_FUNCTION_ARGS);
extern Datum dbms_alert_set_defaults(PG_FUNCTION_ARGS);
extern Datum dbms_alert_signal(PG_FUNCTION_ARGS);
extern Datum dbms_alert_waitany(PG_FUNCTION_ARGS);
extern Datum dbms_alert_waitone(PG_FUNCTION_ARGS);
extern Datum dbms_alert_defered_signal(PG_FUNCTION_ARGS);

/* from assert.c */
extern Datum dbms_assert_enquote_literal(PG_FUNCTION_ARGS);
extern Datum dbms_assert_enquote_name(PG_FUNCTION_ARGS);
extern Datum dbms_assert_noop(PG_FUNCTION_ARGS);
extern Datum dbms_assert_qualified_sql_name(PG_FUNCTION_ARGS);
extern Datum dbms_assert_schema_name(PG_FUNCTION_ARGS);
extern Datum dbms_assert_simple_sql_name(PG_FUNCTION_ARGS);
extern Datum dbms_assert_object_name(PG_FUNCTION_ARGS);

/* from convert.c */
extern Datum orafce_to_char_int4(PG_FUNCTION_ARGS);
extern Datum orafce_to_char_int8(PG_FUNCTION_ARGS);
extern Datum orafce_to_char_float4(PG_FUNCTION_ARGS);
extern Datum orafce_to_char_float8(PG_FUNCTION_ARGS);
extern Datum orafce_to_char_numeric(PG_FUNCTION_ARGS);
extern Datum orafce_to_number(PG_FUNCTION_ARGS);
extern Datum orafce_to_multi_byte(PG_FUNCTION_ARGS);

/* from datefce.c */
extern Datum next_day(PG_FUNCTION_ARGS);
extern Datum next_day_by_index(PG_FUNCTION_ARGS);
extern Datum last_day(PG_FUNCTION_ARGS);
extern Datum months_between(PG_FUNCTION_ARGS);
extern Datum add_months(PG_FUNCTION_ARGS);
extern Datum ora_date_trunc(PG_FUNCTION_ARGS);
extern Datum ora_date_round(PG_FUNCTION_ARGS);
extern Datum ora_timestamptz_trunc(PG_FUNCTION_ARGS);
extern Datum ora_timestamptz_round(PG_FUNCTION_ARGS);

/* from file.c */
extern Datum utl_file_fopen(PG_FUNCTION_ARGS);
extern Datum utl_file_is_open(PG_FUNCTION_ARGS);
extern Datum utl_file_get_line(PG_FUNCTION_ARGS);
extern Datum utl_file_get_nextline(PG_FUNCTION_ARGS);
extern Datum utl_file_put(PG_FUNCTION_ARGS);
extern Datum utl_file_put_line(PG_FUNCTION_ARGS);
extern Datum utl_file_new_line(PG_FUNCTION_ARGS);
extern Datum utl_file_putf(PG_FUNCTION_ARGS);
extern Datum utl_file_fflush(PG_FUNCTION_ARGS);
extern Datum utl_file_fclose(PG_FUNCTION_ARGS);
extern Datum utl_file_fclose_all(PG_FUNCTION_ARGS);
extern Datum utl_file_fremove(PG_FUNCTION_ARGS);
extern Datum utl_file_frename(PG_FUNCTION_ARGS);
extern Datum utl_file_fcopy(PG_FUNCTION_ARGS);
extern Datum utl_file_fgetattr(PG_FUNCTION_ARGS);
extern Datum utl_file_tmpdir(PG_FUNCTION_ARGS);

/* from others.c */
extern Datum ora_nvl(PG_FUNCTION_ARGS);
extern Datum ora_nvl2(PG_FUNCTION_ARGS);
extern Datum ora_concat(PG_FUNCTION_ARGS);
extern Datum ora_nlssort(PG_FUNCTION_ARGS);
extern Datum ora_set_nls_sort(PG_FUNCTION_ARGS);
extern Datum ora_lnnvl(PG_FUNCTION_ARGS);
extern Datum ora_decode(PG_FUNCTION_ARGS);
extern Datum orafce_dump(PG_FUNCTION_ARGS);

/* from pipe.c */
extern Datum dbms_pipe_pack_message_text(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_send_message(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_receive_message(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unique_session_name (PG_FUNCTION_ARGS);
extern Datum dbms_pipe_list_pipes (PG_FUNCTION_ARGS);
extern Datum dbms_pipe_next_item_type (PG_FUNCTION_ARGS);
extern Datum dbms_pipe_create_pipe(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_create_pipe_2(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_create_pipe_1(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_reset_buffer(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_purge(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_remove_pipe(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_date(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_date(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_timestamp(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_timestamp(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_number(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_number(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_bytea(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_bytea(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_record(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_unpack_message_record(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_integer(PG_FUNCTION_ARGS);
extern Datum dbms_pipe_pack_message_bigint(PG_FUNCTION_ARGS);

/* from plunit.c */
extern Datum plunit_assert_true(PG_FUNCTION_ARGS);
extern Datum plunit_assert_true_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_false(PG_FUNCTION_ARGS);
extern Datum plunit_assert_false_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_null(PG_FUNCTION_ARGS);
extern Datum plunit_assert_null_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_null(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_null_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_equals(PG_FUNCTION_ARGS);
extern Datum plunit_assert_equals_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_equals_range(PG_FUNCTION_ARGS);
extern Datum plunit_assert_equals_range_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_equals(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_equals_message(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_equals_range(PG_FUNCTION_ARGS);
extern Datum plunit_assert_not_equals_range_message(PG_FUNCTION_ARGS);
extern Datum plunit_fail(PG_FUNCTION_ARGS);
extern Datum plunit_fail_message(PG_FUNCTION_ARGS);

/* from plvlec.c */
extern Datum plvlex_tokens(PG_FUNCTION_ARGS);

/* from plvstr.c */
extern Datum plvstr_rvrs(PG_FUNCTION_ARGS);
extern Datum plvstr_normalize(PG_FUNCTION_ARGS);
extern Datum plvstr_is_prefix_text(PG_FUNCTION_ARGS);
extern Datum plvstr_is_prefix_int(PG_FUNCTION_ARGS);
extern Datum plvstr_is_prefix_int64(PG_FUNCTION_ARGS);
extern Datum plvstr_lpart(PG_FUNCTION_ARGS);
extern Datum plvstr_rpart(PG_FUNCTION_ARGS);
extern Datum plvstr_lstrip(PG_FUNCTION_ARGS);
extern Datum plvstr_rstrip(PG_FUNCTION_ARGS);
extern Datum plvstr_left(PG_FUNCTION_ARGS);
extern Datum plvstr_right(PG_FUNCTION_ARGS);
extern Datum plvstr_substr2(PG_FUNCTION_ARGS);
extern Datum plvstr_substr3(PG_FUNCTION_ARGS);
extern Datum plvstr_instr2(PG_FUNCTION_ARGS);
extern Datum plvstr_instr3(PG_FUNCTION_ARGS);
extern Datum plvstr_instr4(PG_FUNCTION_ARGS);
extern Datum plvstr_betwn_i(PG_FUNCTION_ARGS);
extern Datum plvstr_betwn_c(PG_FUNCTION_ARGS);
extern Datum plvstr_swap(PG_FUNCTION_ARGS);
extern Datum plvchr_nth(PG_FUNCTION_ARGS);
extern Datum plvchr_first(PG_FUNCTION_ARGS);
extern Datum plvchr_last(PG_FUNCTION_ARGS);
extern Datum plvchr_is_kind_i(PG_FUNCTION_ARGS);
extern Datum plvchr_is_kind_a(PG_FUNCTION_ARGS);
extern Datum plvchr_char_name(PG_FUNCTION_ARGS);
extern Datum oracle_substr2(PG_FUNCTION_ARGS);
extern Datum oracle_substr3(PG_FUNCTION_ARGS);
 
/* from plvsubst.c */
extern Datum plvsubst_string_array(PG_FUNCTION_ARGS);
extern Datum plvsubst_string_string(PG_FUNCTION_ARGS);
extern Datum plvsubst_setsubst(PG_FUNCTION_ARGS);
extern Datum plvsubst_setsubst_default(PG_FUNCTION_ARGS);
extern Datum plvsubst_subst(PG_FUNCTION_ARGS);

/* from putline.c */
extern Datum dbms_output_enable(PG_FUNCTION_ARGS);
extern Datum dbms_output_enable_default(PG_FUNCTION_ARGS);
extern Datum dbms_output_disable(PG_FUNCTION_ARGS);
extern Datum dbms_output_serveroutput(PG_FUNCTION_ARGS);
extern Datum dbms_output_put(PG_FUNCTION_ARGS);
extern Datum dbms_output_put_line(PG_FUNCTION_ARGS);
extern Datum dbms_output_new_line(PG_FUNCTION_ARGS);
extern Datum dbms_output_get_line(PG_FUNCTION_ARGS);
extern Datum dbms_output_get_lines(PG_FUNCTION_ARGS);

/* from random.c */
extern Datum dbms_random_initialize(PG_FUNCTION_ARGS);
extern Datum dbms_random_normal(PG_FUNCTION_ARGS);
extern Datum dbms_random_random(PG_FUNCTION_ARGS);
extern Datum dbms_random_seed_int(PG_FUNCTION_ARGS);
extern Datum dbms_random_seed_varchar(PG_FUNCTION_ARGS);
extern Datum dbms_random_string(PG_FUNCTION_ARGS);
extern Datum dbms_random_terminate(PG_FUNCTION_ARGS);
extern Datum dbms_random_value(PG_FUNCTION_ARGS);
extern Datum dbms_random_value_range(PG_FUNCTION_ARGS);

/* from utility.c */
extern Datum dbms_utility_format_call_stack0(PG_FUNCTION_ARGS);
extern Datum dbms_utility_format_call_stack1(PG_FUNCTION_ARGS);

#endif
