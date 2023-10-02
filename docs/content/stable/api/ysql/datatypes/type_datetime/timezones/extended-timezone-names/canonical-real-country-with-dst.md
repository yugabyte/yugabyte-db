---
title: Real timezones that observe DST [YSQL]
headerTitle: Real timezones that observe Daylight Savings Time
linkTitle: Real timezones with DST
description: Real timezones with DST table. [YSQL]
menu:
  stable:
    identifier: canonical-real-country-with-dst
    parent: extended-timezone-names
    weight: 20
type: docs
---

This table shows canonically-named timezones that are associated with a specific country or region—in other words, they are _real_ timezones. It selects the timezones that observe Daylight Savings Time using the predicate _std_offset <> dst_offset_. The results that the table below presents are based on the view _canonical_real_country_with_dst_ and are ordered by the _utc_offset_ column and then by the _name_ column. Trivial code adds the Markdown table notation. The view is defined thus:

```plpgsql
drop view if exists canonical_real_country_with_dst cascade;

create view canonical_real_country_with_dst as
select
  name,
  std_abbrev,
  dst_abbrev,
  std_offset,
  dst_offset,
  country_code,
  region_coverage
from extended_timezone_names
where
  lower(status) = 'canonical'                                         and

  std_offset <> dst_offset                                            and

  country_code is not null                                            and
  country_code <> ''                                                  and

  lat_long is not null                                                and
  lat_long <> ''                                                      and

  name not like '%0%'                                                 and
  name not like '%1%'                                                 and
  name not like '%2%'                                                 and
  name not like '%3%'                                                 and
  name not like '%4%'                                                 and
  name not like '%5%'                                                 and
  name not like '%6%'                                                 and
  name not like '%7%'                                                 and
  name not like '%8%'                                                 and
  name not like '%9%'                                                 and

  lower(name) not in (select lower(abbrev) from pg_timezone_names)    and
  lower(name) not in (select lower(abbrev) from pg_timezone_abbrevs);
```

Here is the result:

| Name                             | STD abbrev | DST abbrev | STD offset | DST offset | Country code | Region coverage                                                           |
| ----                             | -----------| -----------| ---------- | ---------- | ------------ | ---------------                                                           |
| America/Adak                     | HST        | HDT        | -10:00     | -09:00     | US           | Aleutian Islands                                                          |
| America/Anchorage                | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska (most areas)                                                       |
| America/Juneau                   | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska - Juneau area                                                      |
| America/Metlakatla               | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska - Annette Island                                                   |
| America/Nome                     | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska (west)                                                             |
| America/Sitka                    | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska - Sitka area                                                       |
| America/Yakutat                  | AKST       | AKDT       | -09:00     | -08:00     | US           | Alaska - Yakutat                                                          |
| America/Los_Angeles              | PST        | PDT        | -08:00     | -07:00     | US           | Pacific                                                                   |
| America/Tijuana                  | PST        | PDT        | -08:00     | -07:00     | MX           | Pacific Time US - Baja California                                         |
| America/Vancouver                | PST        | PDT        | -08:00     | -07:00     | CA           | Pacific - BC (most areas)                                                 |
| America/Boise                    | MST        | MDT        | -07:00     | -06:00     | US           | Mountain - ID (south); OR (east)                                          |
| America/Cambridge_Bay            | MST        | MDT        | -07:00     | -06:00     | CA           | Mountain - NU (west)                                                      |
| America/Chihuahua                | MST        | MDT        | -07:00     | -06:00     | MX           | Mountain Time - Chihuahua (most areas)                                    |
| America/Denver                   | MST        | MDT        | -07:00     | -06:00     | US           | Mountain (most areas)                                                     |
| America/Edmonton                 | MST        | MDT        | -07:00     | -06:00     | CA           | Mountain - AB; BC (E); SK (W)                                             |
| America/Inuvik                   | MST        | MDT        | -07:00     | -06:00     | CA           | Mountain - NT (west)                                                      |
| America/Mazatlan                 | MST        | MDT        | -07:00     | -06:00     | MX           | Mountain Time - Baja California Sur, Nayarit, Sinaloa                     |
| America/Ojinaga                  | MST        | MDT        | -07:00     | -06:00     | MX           | Mountain Time US - Chihuahua (US border)                                  |
| America/Yellowknife              | MST        | MDT        | -07:00     | -06:00     | CA           | Mountain - NT (central)                                                   |
| America/Bahia_Banderas           | CST        | CDT        | -06:00     | -05:00     | MX           | Central Time - Bahía de Banderas                                          |
| America/Chicago                  | CST        | CDT        | -06:00     | -05:00     | US           | Central (most areas)                                                      |
| America/Indiana/Knox             | CST        | CDT        | -06:00     | -05:00     | US           | Central - IN (Starke)                                                     |
| America/Indiana/Tell_City        | CST        | CDT        | -06:00     | -05:00     | US           | Central - IN (Perry)                                                      |
| America/Matamoros                | CST        | CDT        | -06:00     | -05:00     | MX           | Central Time US - Coahuila, Nuevo León, Tamaulipas (US border)            |
| America/Menominee                | CST        | CDT        | -06:00     | -05:00     | US           | Central - MI (Wisconsin border)                                           |
| America/Merida                   | CST        | CDT        | -06:00     | -05:00     | MX           | Central Time - Campeche, Yucatán                                          |
| America/Mexico_City              | CST        | CDT        | -06:00     | -05:00     | MX           | Central Time                                                              |
| America/Monterrey                | CST        | CDT        | -06:00     | -05:00     | MX           | Central Time - Durango; Coahuila, Nuevo León, Tamaulipas (most areas)     |
| America/North_Dakota/Beulah      | CST        | CDT        | -06:00     | -05:00     | US           | Central - ND (Mercer)                                                     |
| America/North_Dakota/Center      | CST        | CDT        | -06:00     | -05:00     | US           | Central - ND (Oliver)                                                     |
| America/North_Dakota/New_Salem   | CST        | CDT        | -06:00     | -05:00     | US           | Central - ND (Morton rural)                                               |
| America/Rainy_River              | CST        | CDT        | -06:00     | -05:00     | CA           | Central - ON (Rainy R, Ft Frances)                                        |
| America/Rankin_Inlet             | CST        | CDT        | -06:00     | -05:00     | CA           | Central - NU (central)                                                    |
| America/Resolute                 | CST        | CDT        | -06:00     | -05:00     | CA           | Central - NU (Resolute)                                                   |
| America/Winnipeg                 | CST        | CDT        | -06:00     | -05:00     | CA           | Central - ON (west); Manitoba                                             |
| Pacific/Easter                   | -06        | -05        | -06:00     | -05:00     | CL           | Easter Island                                                             |
| America/Detroit                  | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - MI (most areas)                                                 |
| America/Grand_Turk               | EST        | EDT        | -05:00     | -04:00     | TC           |                                                                           |
| America/Havana                   | CST        | CDT        | -05:00     | -04:00     | CU           |                                                                           |
| America/Indiana/Indianapolis     | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (most areas)                                                 |
| America/Indiana/Marengo          | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (Crawford)                                                   |
| America/Indiana/Petersburg       | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (Pike)                                                       |
| America/Indiana/Vevay            | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (Switzerland)                                                |
| America/Indiana/Vincennes        | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (Da, Du, K, Mn)                                              |
| America/Indiana/Winamac          | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - IN (Pulaski)                                                    |
| America/Iqaluit                  | EST        | EDT        | -05:00     | -04:00     | CA           | Eastern - NU (most east areas)                                            |
| America/Kentucky/Louisville      | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - KY (Louisville area)                                            |
| America/Kentucky/Monticello      | EST        | EDT        | -05:00     | -04:00     | US           | Eastern - KY (Wayne)                                                      |
| America/Nassau                   | EST        | EDT        | -05:00     | -04:00     | BS           |                                                                           |
| America/New_York                 | EST        | EDT        | -05:00     | -04:00     | US           | Eastern (most areas)                                                      |
| America/Nipigon                  | EST        | EDT        | -05:00     | -04:00     | CA           | Eastern - ON, QC (no DST 1967-73)                                         |
| America/Pangnirtung              | EST        | EDT        | -05:00     | -04:00     | CA           | Eastern - NU (Pangnirtung)                                                |
| America/Port-au-Prince           | EST        | EDT        | -05:00     | -04:00     | HT           |                                                                           |
| America/Thunder_Bay              | EST        | EDT        | -05:00     | -04:00     | CA           | Eastern - ON (Thunder Bay)                                                |
| America/Toronto                  | EST        | EDT        | -05:00     | -04:00     | CA           | Eastern - ON, QC (most areas)                                             |
| America/Asuncion                 | -04        | -03        | -04:00     | -03:00     | PY           |                                                                           |
| America/Glace_Bay                | AST        | ADT        | -04:00     | -03:00     | CA           | Atlantic - NS (Cape Breton)                                               |
| America/Goose_Bay                | AST        | ADT        | -04:00     | -03:00     | CA           | Atlantic - Labrador (most areas)                                          |
| America/Halifax                  | AST        | ADT        | -04:00     | -03:00     | CA           | Atlantic - NS (most areas); PE                                            |
| America/Moncton                  | AST        | ADT        | -04:00     | -03:00     | CA           | Atlantic - New Brunswick                                                  |
| America/Santiago                 | -04        | -03        | -04:00     | -03:00     | CL           | Chile (most areas)                                                        |
| America/Thule                    | AST        | ADT        | -04:00     | -03:00     | GL           | Thule/Pituffik                                                            |
| Atlantic/Bermuda                 | AST        | ADT        | -04:00     | -03:00     | BM           |                                                                           |
| America/St_Johns                 | NST        | NDT        | -03:30     | -02:30     | CA           | Newfoundland; Labrador (southeast)                                        |
| America/Miquelon                 | -03        | -02        | -03:00     | -02:00     | PM           |                                                                           |
| America/Scoresbysund             | -01        | +00        | -01:00     |  00:00     | GL           | Scoresbysund/Ittoqqortoormiit                                             |
| Atlantic/Azores                  | -01        | +00        | -01:00     |  00:00     | PT           | Azores                                                                    |
| Antarctica/Troll                 | +00        | +02        |  00:00     |  02:00     | AQ           | Troll                                                                     |
| Atlantic/Canary                  | WET        | WEST       |  00:00     |  01:00     | ES           | Canary Islands                                                            |
| Atlantic/Faroe                   | WET        | WEST       |  00:00     |  01:00     | FO           |                                                                           |
| Atlantic/Madeira                 | WET        | WEST       |  00:00     |  01:00     | PT           | Madeira Islands                                                           |
| Europe/Dublin                    | GMT        | IST        |  00:00     |  01:00     | IE           |                                                                           |
| Europe/Lisbon                    | WET        | WEST       |  00:00     |  01:00     | PT           | Portugal (mainland)                                                       |
| Europe/London                    | GMT        | BST        |  00:00     |  01:00     | GB           |                                                                           |
| Africa/Ceuta                     | CET        | CEST       |  01:00     |  02:00     | ES           | Ceuta, Melilla                                                            |
| Europe/Amsterdam                 | CET        | CEST       |  01:00     |  02:00     | NL           |                                                                           |
| Europe/Andorra                   | CET        | CEST       |  01:00     |  02:00     | AD           |                                                                           |
| Europe/Belgrade                  | CET        | CEST       |  01:00     |  02:00     | RS           |                                                                           |
| Europe/Berlin                    | CET        | CEST       |  01:00     |  02:00     | DE           | Germany (most areas)                                                      |
| Europe/Brussels                  | CET        | CEST       |  01:00     |  02:00     | BE           |                                                                           |
| Europe/Budapest                  | CET        | CEST       |  01:00     |  02:00     | HU           |                                                                           |
| Europe/Copenhagen                | CET        | CEST       |  01:00     |  02:00     | DK           |                                                                           |
| Europe/Gibraltar                 | CET        | CEST       |  01:00     |  02:00     | GI           |                                                                           |
| Europe/Luxembourg                | CET        | CEST       |  01:00     |  02:00     | LU           |                                                                           |
| Europe/Madrid                    | CET        | CEST       |  01:00     |  02:00     | ES           | Spain (mainland)                                                          |
| Europe/Malta                     | CET        | CEST       |  01:00     |  02:00     | MT           |                                                                           |
| Europe/Monaco                    | CET        | CEST       |  01:00     |  02:00     | MC           |                                                                           |
| Europe/Oslo                      | CET        | CEST       |  01:00     |  02:00     | NO           |                                                                           |
| Europe/Paris                     | CET        | CEST       |  01:00     |  02:00     | FR           |                                                                           |
| Europe/Prague                    | CET        | CEST       |  01:00     |  02:00     | CZ           |                                                                           |
| Europe/Rome                      | CET        | CEST       |  01:00     |  02:00     | IT           |                                                                           |
| Europe/Stockholm                 | CET        | CEST       |  01:00     |  02:00     | SE           |                                                                           |
| Europe/Tirane                    | CET        | CEST       |  01:00     |  02:00     | AL           |                                                                           |
| Europe/Vienna                    | CET        | CEST       |  01:00     |  02:00     | AT           |                                                                           |
| Europe/Warsaw                    | CET        | CEST       |  01:00     |  02:00     | PL           |                                                                           |
| Europe/Zurich                    | CET        | CEST       |  01:00     |  02:00     | CH           | Swiss time                                                                |
| Asia/Amman                       | EET        | EEST       |  02:00     |  03:00     | JO           |                                                                           |
| Asia/Beirut                      | EET        | EEST       |  02:00     |  03:00     | LB           |                                                                           |
| Asia/Damascus                    | EET        | EEST       |  02:00     |  03:00     | SY           |                                                                           |
| Asia/Famagusta                   | EET        | EEST       |  02:00     |  03:00     | CY           | Northern Cyprus                                                           |
| Asia/Gaza                        | EET        | EEST       |  02:00     |  03:00     | PS           | Gaza Strip                                                                |
| Asia/Hebron                      | EET        | EEST       |  02:00     |  03:00     | PS           | West Bank                                                                 |
| Asia/Jerusalem                   | IST        | IDT        |  02:00     |  03:00     | IL           |                                                                           |
| Asia/Nicosia                     | EET        | EEST       |  02:00     |  03:00     | CY           | Cyprus (most areas)                                                       |
| Europe/Athens                    | EET        | EEST       |  02:00     |  03:00     | GR           |                                                                           |
| Europe/Bucharest                 | EET        | EEST       |  02:00     |  03:00     | RO           |                                                                           |
| Europe/Chisinau                  | EET        | EEST       |  02:00     |  03:00     | MD           |                                                                           |
| Europe/Helsinki                  | EET        | EEST       |  02:00     |  03:00     | FI           |                                                                           |
| Europe/Kiev                      | EET        | EEST       |  02:00     |  03:00     | UA           | Ukraine (most areas)                                                      |
| Europe/Riga                      | EET        | EEST       |  02:00     |  03:00     | LV           |                                                                           |
| Europe/Sofia                     | EET        | EEST       |  02:00     |  03:00     | BG           |                                                                           |
| Europe/Tallinn                   | EET        | EEST       |  02:00     |  03:00     | EE           |                                                                           |
| Europe/Uzhgorod                  | EET        | EEST       |  02:00     |  03:00     | UA           | Transcarpathia                                                            |
| Europe/Vilnius                   | EET        | EEST       |  02:00     |  03:00     | LT           |                                                                           |
| Europe/Zaporozhye                | EET        | EEST       |  02:00     |  03:00     | UA           | Zaporozhye and east Lugansk                                               |
| Asia/Tehran                      | +0330      | +0430      |  03:30     |  04:30     | IR           |                                                                           |
| Australia/Adelaide               | ACST       | ACDT       |  09:30     |  10:30     | AU           | South Australia                                                           |
| Australia/Broken_Hill            | ACST       | ACDT       |  09:30     |  10:30     | AU           | New South Wales (Yancowinna)                                              |
| Australia/Hobart                 | AEST       | AEDT       |  10:00     |  11:00     | AU           | Tasmania                                                                  |
| Australia/Melbourne              | AEST       | AEDT       |  10:00     |  11:00     | AU           | Victoria                                                                  |
| Australia/Sydney                 | AEST       | AEDT       |  10:00     |  11:00     | AU           | New South Wales (most areas)                                              |
| Australia/Lord_Howe              | +1030      | +11        |  10:30     |  11:00     | AU           | Lord Howe Island                                                          |
| Pacific/Auckland                 | NZST       | NZDT       |  12:00     |  13:00     | NZ           | New Zealand time                                                          |
| Pacific/Fiji                     | +12        | +13        |  12:00     |  13:00     | FJ           |                                                                           |
| Pacific/Chatham                  | +1245      | +1345      |  12:45     |  13:45     | NZ           | Chatham Islands                                                           |
| Pacific/Apia                     | +13        | +14        |  13:00     |  14:00     | WS           |                                                                           |
