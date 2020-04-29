
#ifndef ORAFCE_BUILTINS
#define ORAFCE_BUILTINS

#ifndef PGDLLEXPORT
#ifdef _MSC_VER
#define PGDLLEXPORT	__declspec(dllexport)

/*
 * PG_MODULE_MAGIC and PG_FUNCTION_INFO_V1 macros are broken for MSVC.
 * So, we redefine them.
 */

#undef PG_MODULE_MAGIC
#define PG_MODULE_MAGIC \
extern PGDLLEXPORT const Pg_magic_struct *PG_MAGIC_FUNCTION_NAME(void); \
const Pg_magic_struct * \
PG_MAGIC_FUNCTION_NAME(void) \
{ \
	static const Pg_magic_struct Pg_magic_data = PG_MODULE_MAGIC_DATA; \
	return &Pg_magic_data; \
} \
extern int no_such_variable

#undef PG_FUNCTION_INFO_V1
#define PG_FUNCTION_INFO_V1(funcname) \
extern PGDLLEXPORT const Pg_finfo_record * CppConcat(pg_finfo_,funcname)(void); \
const Pg_finfo_record * \
CppConcat(pg_finfo_,funcname) (void) \
{ \
	static const Pg_finfo_record my_finfo = { 1 }; \
	return &my_finfo; \
} \
extern int no_such_variable

#else
#define PGDLLEXPORT	PGDLLIMPORT
#endif
#endif

/* from aggregate.c */
extern PGDLLEXPORT Datum orafce_listagg1_transfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_wm_concat_transfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_listagg2_transfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_listagg_finalfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_median4_transfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_median4_finalfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_median8_transfn(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_median8_finalfn(PG_FUNCTION_ARGS);

/* from alert.c */
extern PGDLLEXPORT Datum dbms_alert_register(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_remove(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_removeall(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_set_defaults(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_signal(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_waitany(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_waitone(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_alert_defered_signal(PG_FUNCTION_ARGS);

/* from assert.c */
extern PGDLLEXPORT Datum dbms_assert_enquote_literal(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_enquote_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_noop(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_qualified_sql_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_schema_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_simple_sql_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_assert_object_name(PG_FUNCTION_ARGS);

/* from convert.c */
extern PGDLLEXPORT Datum orafce_to_char_int4(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_char_int8(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_char_float4(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_char_float8(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_char_numeric(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_char_timestamp(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_number(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_multi_byte(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_to_single_byte(PG_FUNCTION_ARGS);

/* from datefce.c */
extern PGDLLEXPORT Datum next_day(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum next_day_by_index(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum last_day(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum months_between(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum add_months(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_to_date(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_date_trunc(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_date_round(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_timestamptz_trunc(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_timestamptz_round(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_timestamp_trunc(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_timestamp_round(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_sysdate(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_sessiontimezone(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_dbtimezone(PG_FUNCTION_ARGS);

/* from file.c */
extern PGDLLEXPORT Datum utl_file_fopen(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_is_open(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_get_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_get_nextline(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_put(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_put_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_new_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_putf(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fflush(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fclose(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fclose_all(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fremove(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_frename(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fcopy(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_fgetattr(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum utl_file_tmpdir(PG_FUNCTION_ARGS);

/* from others.c */
extern PGDLLEXPORT Datum ora_nvl(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_nvl2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_concat(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_nlssort(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_set_nls_sort(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_lnnvl(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_decode(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_dump(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_get_major_version(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_get_major_version_num(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_get_full_version_num(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_get_platform(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum ora_get_status(PG_FUNCTION_ARGS);

/* from pipe.c */
extern PGDLLEXPORT Datum dbms_pipe_pack_message_text(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_text(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_send_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_receive_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unique_session_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_list_pipes(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_next_item_type(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_create_pipe(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_create_pipe_2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_create_pipe_1(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_reset_buffer(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_purge(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_remove_pipe(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_date(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_date(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_timestamp(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_timestamp(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_number(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_number(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_bytea(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_bytea(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_record(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_unpack_message_record(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_integer(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_pipe_pack_message_bigint(PG_FUNCTION_ARGS);

/* from plunit.c */
extern PGDLLEXPORT Datum plunit_assert_true(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_true_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_false(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_false_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_null(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_null_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_null(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_null_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_equals(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_equals_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_equals_range(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_equals_range_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_equals(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_equals_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_equals_range(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_assert_not_equals_range_message(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_fail(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plunit_fail_message(PG_FUNCTION_ARGS);

/* from plvdate.c */
extern PGDLLEXPORT Datum plvdate_add_bizdays(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_nearest_bizday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_next_bizday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_bizdays_between(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_prev_bizday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_isbizday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_set_nonbizday_dow(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_unset_nonbizday_dow(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_set_nonbizday_day(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_unset_nonbizday_day(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_use_easter(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_using_easter(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_use_great_friday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_using_great_friday(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_include_start(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_including_start(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_default_holidays(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_version(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_days_inmonth(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvdate_isleapyear(PG_FUNCTION_ARGS);

/* from plvlec.c */
extern PGDLLEXPORT Datum plvlex_tokens(PG_FUNCTION_ARGS);

/* from plvstr.c */
extern PGDLLEXPORT Datum plvstr_rvrs(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_normalize(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_is_prefix_text(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_is_prefix_int(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_is_prefix_int64(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_lpart(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_rpart(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_lstrip(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_rstrip(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_left(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_right(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_substr2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_substr3(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_instr2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_instr3(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_instr4(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_betwn_i(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_betwn_c(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvstr_swap(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_nth(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_first(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_last(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_is_kind_i(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_is_kind_a(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvchr_char_name(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum oracle_substr2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum oracle_substr3(PG_FUNCTION_ARGS);
 
/* from plvsubst.c */
extern PGDLLEXPORT Datum plvsubst_string_array(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvsubst_string_string(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvsubst_setsubst(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvsubst_setsubst_default(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum plvsubst_subst(PG_FUNCTION_ARGS);

/* from putline.c */
extern PGDLLEXPORT Datum dbms_output_enable(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_enable_default(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_disable(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_serveroutput(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_put(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_put_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_new_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_get_line(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_output_get_lines(PG_FUNCTION_ARGS);

/* from random.c */
extern PGDLLEXPORT Datum dbms_random_initialize(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_normal(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_random(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_seed_int(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_seed_varchar(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_string(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_terminate(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_value(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_random_value_range(PG_FUNCTION_ARGS);

/* from utility.c */
extern PGDLLEXPORT Datum dbms_utility_format_call_stack0(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum dbms_utility_format_call_stack1(PG_FUNCTION_ARGS);

/* from oraguc.c */
extern void PGDLLEXPORT _PG_init(void);

/* from charpad.c */
extern PGDLLEXPORT Datum orafce_lpad(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_rpad(PG_FUNCTION_ARGS);

/* from charlen.c */
extern PGDLLEXPORT Datum orafce_bpcharlen(PG_FUNCTION_ARGS);

/* from varchar2.c */
extern PGDLLEXPORT Datum varchar2in(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum varchar2out(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum varchar2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum varchar2recv(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_concat2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_varchar_transform(PG_FUNCTION_ARGS);

/* from nvarchar2.c */
extern PGDLLEXPORT Datum nvarchar2in(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum nvarchar2out(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum nvarchar2(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum nvarchar2recv(PG_FUNCTION_ARGS);

/* from replace_empty_string.c */
extern PGDLLEXPORT Datum orafce_replace_empty_strings(PG_FUNCTION_ARGS);
extern PGDLLEXPORT Datum orafce_replace_null_strings(PG_FUNCTION_ARGS);

#endif
