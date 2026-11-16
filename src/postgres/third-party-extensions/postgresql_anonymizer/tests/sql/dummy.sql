BEGIN;

CREATE EXTENSION anon;

-- Address

SELECT anon.dummy_city_prefix() IS NOT NULL;
SELECT anon.dummy_city_suffix() IS NOT NULL;
SELECT anon.dummy_city_name() IS NOT NULL;
SELECT anon.dummy_country_name() IS NOT NULL;
SELECT anon.dummy_country_code() IS NOT NULL;
SELECT anon.dummy_street_suffix() IS NOT NULL;
SELECT anon.dummy_street_name() IS NOT NULL;
SELECT anon.dummy_timezone() IS NOT NULL;
SELECT anon.dummy_state_name() IS NOT NULL;
SELECT anon.dummy_state_abbr() IS NOT NULL;
SELECT anon.dummy_secondary_address_type() IS NOT NULL;
SELECT anon.dummy_secondary_address() IS NOT NULL;
SELECT anon.dummy_zip_code() IS NOT NULL;
SELECT anon.dummy_post_code() IS NOT NULL;
SELECT anon.dummy_building_number() IS NOT NULL;
SELECT anon.dummy_latitude() IS NOT NULL;
SELECT anon.dummy_longitude() IS NOT NULL;

-- Administrative

SELECT anon.dummy_health_insurance_code() IS NOT NULL;

-- Automotive

SELECT anon.dummy_licence_plate() IS NOT NULL;

-- Barcode

SELECT anon.dummy_isbn() IS NOT NULL;
SELECT anon.dummy_isbn() IS NOT NULL;

-- Color

SELECT anon.dummy_hex_color() IS NOT NULL;
SELECT anon.dummy_rgb_color() IS NOT NULL;
SELECT anon.dummy_rgba_color() IS NOT NULL;
SELECT anon.dummy_hsl_color() IS NOT NULL;
SELECT anon.dummy_hsla_color() IS NOT NULL;
SELECT anon.dummy_color() IS NOT NULL;

-- Company

SELECT anon.dummy_company_suffix() IS NOT NULL;
SELECT anon.dummy_company_name() IS NOT NULL;
SELECT anon.dummy_buzzword() IS NOT NULL;
SELECT anon.dummy_buzzword_middle() IS NOT NULL;
SELECT anon.dummy_buzzword_tail() IS NOT NULL;
SELECT anon.dummy_catchphrase() IS NOT NULL;
SELECT anon.dummy_bs_verb() IS NOT NULL;
SELECT anon.dummy_bs_adj() IS NOT NULL;
SELECT anon.dummy_bs_noun() IS NOT NULL;
SELECT anon.dummy_bs() IS NOT NULL;
SELECT anon.dummy_profession() IS NOT NULL;
SELECT anon.dummy_industry() IS NOT NULL;

-- Creditcard

SELECT anon.dummy_credit_card_number() IS NOT NULL;

-- Currency

SELECT anon.dummy_currency_code() IS NOT NULL;
SELECT anon.dummy_currency_name() IS NOT NULL;
SELECT anon.dummy_currency_symbol() IS NOT NULL;

-- Filesystem

SELECT anon.dummy_file_path() IS NOT NULL;
SELECT anon.dummy_file_name() IS NOT NULL;
SELECT anon.dummy_file_extension() IS NOT NULL;
SELECT anon.dummy_dir_path() IS NOT NULL;

-- Finance

SELECT anon.dummy_bic() IS NOT NULL;
SELECT anon.dummy_isin() IS NOT NULL;

-- HTTP

SELECT anon.dummy_rfc_status_code() IS NOT NULL;
SELECT anon.dummy_valid_statux_code() IS NOT NULL;

-- Internet

SELECT anon.dummy_free_email_provider() IS NOT NULL;
SELECT anon.dummy_domain_suffix() IS NOT NULL;
SELECT anon.dummy_free_email() IS NOT NULL;
SELECT anon.dummy_safe_email() IS NOT NULL;
SELECT anon.dummy_username() IS NOT NULL;
SELECT anon.dummy_ipv4() IS NOT NULL;
SELECT anon.dummy_ipv6() IS NOT NULL;
SELECT anon.dummy_ip() IS NOT NULL;
SELECT anon.dummy_mac_address() IS NOT NULL;
SELECT anon.dummy_user_agent() IS NOT NULL;

-- Lorem

SELECT anon.dummy_word() IS NOT NULL;

-- Person

SELECT anon.dummy_first_name() IS NOT NULL;
SELECT anon.dummy_last_name() IS NOT NULL;
SELECT anon.dummy_title() IS NOT NULL;
SELECT anon.dummy_suffix() IS NOT NULL;
SELECT anon.dummy_name() IS NOT NULL;
SELECT anon.dummy_name_with_title() IS NOT NULL;

-- Phone Number

SELECT anon.dummy_phone_number() IS NOT NULL;
SELECT anon.dummy_cell_number() IS NOT NULL;

-- UUID

SELECT anon.dummy_uuidv1() IS NOT NULL;
SELECT anon.dummy_uuidv3() IS NOT NULL;
SELECT anon.dummy_uuidv4() IS NOT NULL;
SELECT anon.dummy_uuidv5() IS NOT NULL;


-- Global Dummy Locale

SET anon.dummy_locale = 'ja_JP';

SELECT anon.dummy_first_name() !~ '^[[:ascii:]]*$';

SET anon.dummy_locale = 'en_US';

SELECT anon.dummy_first_name() ~ '^[[:ascii:]]*$';

-- Localized faking

SELECT anon.dummy_first_name_locale('fr_FR') IS NOT NULL;
SELECT anon.dummy_first_name_locale('ar_SA') IS NOT NULL;
SELECT anon.dummy_first_name_locale('ja_JP') IS NOT NULL;

ROLLBACK;
