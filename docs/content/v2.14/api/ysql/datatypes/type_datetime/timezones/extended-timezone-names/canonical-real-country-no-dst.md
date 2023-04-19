---
title: Real timezones that do not observe DST [YSQL]
headerTitle: Real timezones that don't observe Daylight Savings Time
linkTitle: Real timezones no DST
description: Table. [YSQL]
menu:
  v2.14:
    identifier: canonical-real-country-no-dst
    parent: extended-timezone-names
    weight: 30
type: docs
---

This table shows canonically-named timezones that are associated with a specific country or region—in other words, they are _real_ timezones. It selects the timezones that observe Daylight Savings Time using the predicate _std_offset = dst_offset_. The results that the table below presents are based on the view _canonical_real_country_no_dst_ and are ordered by the _utc_offset_ column and then by the _name_ column. Trivial code adds the Markdown table notation. The view is defined thus:

```plpgsql
drop view if exists canonical_real_country_no_dst cascade;

create view canonical_real_country_no_dst as
select
  name,
  abbrev,
  utc_offset,
  country_code,
  region_coverage
from extended_timezone_names
where
  lower(status) = 'canonical'                                         and

  std_offset = dst_offset                                             and

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

| Name                             | Abbrev     | UTC offset | Country code | Region coverage                                                           |
| ----                             | -----------| ---------- | ------------ | ---------------                                                           |
| Pacific/Niue                     | -11        | -11:00     | NU           |                                                                           |
| Pacific/Pago_Pago                | SST        | -11:00     | AS           | Samoa, Midway                                                             |
| Pacific/Honolulu                 | HST        | -10:00     | US           | Hawaii                                                                    |
| Pacific/Rarotonga                | -10        | -10:00     | CK           |                                                                           |
| Pacific/Tahiti                   | -10        | -10:00     | PF           | Society Islands                                                           |
| Pacific/Marquesas                | -0930      | -09:30     | PF           | Marquesas Islands                                                         |
| Pacific/Gambier                  | -09        | -09:00     | PF           | Gambier Islands                                                           |
| Pacific/Pitcairn                 | -08        | -08:00     | PN           |                                                                           |
| America/Creston                  | MST        | -07:00     | CA           | MST - BC (Creston)                                                        |
| America/Dawson_Creek             | MST        | -07:00     | CA           | MST - BC (Dawson Cr, Ft St John)                                          |
| America/Fort_Nelson              | MST        | -07:00     | CA           | MST - BC (Ft Nelson)                                                      |
| America/Hermosillo               | MST        | -07:00     | MX           | Mountain Standard Time - Sonora                                           |
| America/Phoenix                  | MST        | -07:00     | US           | MST - Arizona (except Navajo)                                             |
| America/Belize                   | CST        | -06:00     | BZ           |                                                                           |
| America/Costa_Rica               | CST        | -06:00     | CR           |                                                                           |
| America/El_Salvador              | CST        | -06:00     | SV           |                                                                           |
| America/Guatemala                | CST        | -06:00     | GT           |                                                                           |
| America/Managua                  | CST        | -06:00     | NI           |                                                                           |
| America/Regina                   | CST        | -06:00     | CA           | CST - SK (most areas)                                                     |
| America/Swift_Current            | CST        | -06:00     | CA           | CST - SK (midwest)                                                        |
| America/Tegucigalpa              | CST        | -06:00     | HN           |                                                                           |
| Pacific/Galapagos                | -06        | -06:00     | EC           | Galápagos Islands                                                         |
| America/Atikokan                 | EST        | -05:00     | CA           | EST - ON (Atikokan); NU (Coral H)                                         |
| America/Bogota                   | -05        | -05:00     | CO           |                                                                           |
| America/Cancun                   | EST        | -05:00     | MX           | Eastern Standard Time - Quintana Roo                                      |
| America/Eirunepe                 | -05        | -05:00     | BR           | Amazonas (west)                                                           |
| America/Guayaquil                | -05        | -05:00     | EC           | Ecuador (mainland)                                                        |
| America/Jamaica                  | EST        | -05:00     | JM           |                                                                           |
| America/Lima                     | -05        | -05:00     | PE           |                                                                           |
| America/Panama                   | EST        | -05:00     | PA           |                                                                           |
| America/Rio_Branco               | -05        | -05:00     | BR           | Acre                                                                      |
| America/Barbados                 | AST        | -04:00     | BB           |                                                                           |
| America/Blanc-Sablon             | AST        | -04:00     | CA           | AST - QC (Lower North Shore)                                              |
| America/Boa_Vista                | -04        | -04:00     | BR           | Roraima                                                                   |
| America/Caracas                  | -04        | -04:00     | VE           |                                                                           |
| America/Curacao                  | AST        | -04:00     | CW           |                                                                           |
| America/Guyana                   | -04        | -04:00     | GY           |                                                                           |
| America/La_Paz                   | -04        | -04:00     | BO           |                                                                           |
| America/Manaus                   | -04        | -04:00     | BR           | Amazonas (east)                                                           |
| America/Martinique               | AST        | -04:00     | MQ           |                                                                           |
| America/Port_of_Spain            | AST        | -04:00     | TT           |                                                                           |
| America/Porto_Velho              | -04        | -04:00     | BR           | Rondônia                                                                  |
| America/Puerto_Rico              | AST        | -04:00     | PR           |                                                                           |
| America/Santo_Domingo            | AST        | -04:00     | DO           |                                                                           |
| America/Araguaina                | -03        | -03:00     | BR           | Tocantins                                                                 |
| America/Argentina/Buenos_Aires   | -03        | -03:00     | AR           | Buenos Aires (BA, CF)                                                     |
| America/Argentina/Catamarca      | -03        | -03:00     | AR           | Catamarca (CT); Chubut (CH)                                               |
| America/Argentina/Cordoba        | -03        | -03:00     | AR           | Argentina (most areas: CB, CC, CN, ER, FM, MN, SE, SF)                    |
| America/Argentina/Jujuy          | -03        | -03:00     | AR           | Jujuy (JY)                                                                |
| America/Argentina/La_Rioja       | -03        | -03:00     | AR           | La Rioja (LR)                                                             |
| America/Argentina/Mendoza        | -03        | -03:00     | AR           | Mendoza (MZ)                                                              |
| America/Argentina/Rio_Gallegos   | -03        | -03:00     | AR           | Santa Cruz (SC)                                                           |
| America/Argentina/Salta          | -03        | -03:00     | AR           | Salta (SA, LP, NQ, RN)                                                    |
| America/Argentina/San_Juan       | -03        | -03:00     | AR           | San Juan (SJ)                                                             |
| America/Argentina/San_Luis       | -03        | -03:00     | AR           | San Luis (SL)                                                             |
| America/Argentina/Tucuman        | -03        | -03:00     | AR           | Tucumán (TM)                                                              |
| America/Argentina/Ushuaia        | -03        | -03:00     | AR           | Tierra del Fuego (TF)                                                     |
| America/Bahia                    | -03        | -03:00     | BR           | Bahia                                                                     |
| America/Belem                    | -03        | -03:00     | BR           | Pará (east); Amapá                                                        |
| America/Cayenne                  | -03        | -03:00     | GF           |                                                                           |
| America/Fortaleza                | -03        | -03:00     | BR           | Brazil (northeast: MA, PI, CE, RN, PB)                                    |
| America/Maceio                   | -03        | -03:00     | BR           | Alagoas, Sergipe                                                          |
| America/Montevideo               | -03        | -03:00     | UY           |                                                                           |
| America/Paramaribo               | -03        | -03:00     | SR           |                                                                           |
| America/Punta_Arenas             | -03        | -03:00     | CL           | Region of Magallanes                                                      |
| America/Recife                   | -03        | -03:00     | BR           | Pernambuco                                                                |
| America/Santarem                 | -03        | -03:00     | BR           | Pará (west)                                                               |
| Antarctica/Palmer                | -03        | -03:00     | AQ           | Palmer                                                                    |
| Antarctica/Rothera               | -03        | -03:00     | AQ           | Rothera                                                                   |
| Atlantic/Stanley                 | -03        | -03:00     | FK           |                                                                           |
| America/Noronha                  | -02        | -02:00     | BR           | Atlantic islands                                                          |
| Atlantic/South_Georgia           | -02        | -02:00     | GS           |                                                                           |
| Atlantic/Cape_Verde              | -01        | -01:00     | CV           |                                                                           |
| Africa/Abidjan                   | GMT        |  00:00     | CI           |                                                                           |
| Africa/Accra                     | GMT        |  00:00     | GH           |                                                                           |
| Africa/Bissau                    | GMT        |  00:00     | GW           |                                                                           |
| Africa/Monrovia                  | GMT        |  00:00     | LR           |                                                                           |
| Africa/Sao_Tome                  | GMT        |  00:00     | ST           |                                                                           |
| America/Danmarkshavn             | GMT        |  00:00     | GL           | National Park (east coast)                                                |
| Atlantic/Reykjavik               | GMT        |  00:00     | IS           |                                                                           |
| Africa/Algiers                   | CET        |  01:00     | DZ           |                                                                           |
| Africa/Lagos                     | WAT        |  01:00     | NG           | West Africa Time                                                          |
| Africa/Ndjamena                  | WAT        |  01:00     | TD           |                                                                           |
| Africa/Tunis                     | CET        |  01:00     | TN           |                                                                           |
| Africa/Cairo                     | EET        |  02:00     | EG           |                                                                           |
| Africa/Johannesburg              | SAST       |  02:00     | ZA           |                                                                           |
| Africa/Khartoum                  | CAT        |  02:00     | SD           |                                                                           |
| Africa/Maputo                    | CAT        |  02:00     | MZ           | Central Africa Time                                                       |
| Africa/Tripoli                   | EET        |  02:00     | LY           |                                                                           |
| Africa/Windhoek                  | CAT        |  02:00     | NA           |                                                                           |
| Europe/Kaliningrad               | EET        |  02:00     | RU           | MSK-01 - Kaliningrad                                                      |
| Africa/Nairobi                   | EAT        |  03:00     | KE           |                                                                           |
| Antarctica/Syowa                 | +03        |  03:00     | AQ           | Syowa                                                                     |
| Asia/Baghdad                     | +03        |  03:00     | IQ           |                                                                           |
| Asia/Qatar                       | +03        |  03:00     | QA           |                                                                           |
| Asia/Riyadh                      | +03        |  03:00     | SA           |                                                                           |
| Europe/Istanbul                  | +03        |  03:00     | TR           |                                                                           |
| Europe/Kirov                     | +03        |  03:00     | RU           | MSK+00 - Kirov                                                            |
| Europe/Minsk                     | +03        |  03:00     | BY           |                                                                           |
| Europe/Moscow                    | MSK        |  03:00     | RU           | MSK+00 - Moscow area                                                      |
| Europe/Simferopol                | MSK        |  03:00     | UA           | Crimea                                                                    |
| Asia/Baku                        | +04        |  04:00     | AZ           |                                                                           |
| Asia/Dubai                       | +04        |  04:00     | AE           |                                                                           |
| Asia/Tbilisi                     | +04        |  04:00     | GE           |                                                                           |
| Asia/Yerevan                     | +04        |  04:00     | AM           |                                                                           |
| Europe/Astrakhan                 | +04        |  04:00     | RU           | MSK+01 - Astrakhan                                                        |
| Europe/Samara                    | +04        |  04:00     | RU           | MSK+01 - Samara, Udmurtia                                                 |
| Europe/Saratov                   | +04        |  04:00     | RU           | MSK+01 - Saratov                                                          |
| Europe/Ulyanovsk                 | +04        |  04:00     | RU           | MSK+01 - Ulyanovsk                                                        |
| Indian/Mahe                      | +04        |  04:00     | SC           |                                                                           |
| Indian/Mauritius                 | +04        |  04:00     | MU           |                                                                           |
| Indian/Reunion                   | +04        |  04:00     | RE           | Réunion, Crozet, Scattered Islands                                        |
| Asia/Kabul                       | +0430      |  04:30     | AF           |                                                                           |
| Antarctica/Mawson                | +05        |  05:00     | AQ           | Mawson                                                                    |
| Asia/Aqtau                       | +05        |  05:00     | KZ           | Mangghystaū/Mankistau                                                     |
| Asia/Aqtobe                      | +05        |  05:00     | KZ           | Aqtöbe/Aktobe                                                             |
| Asia/Ashgabat                    | +05        |  05:00     | TM           |                                                                           |
| Asia/Atyrau                      | +05        |  05:00     | KZ           | Atyraū/Atirau/Gur'yev                                                     |
| Asia/Dushanbe                    | +05        |  05:00     | TJ           |                                                                           |
| Asia/Karachi                     | PKT        |  05:00     | PK           |                                                                           |
| Asia/Oral                        | +05        |  05:00     | KZ           | West Kazakhstan                                                           |
| Asia/Qyzylorda                   | +05        |  05:00     | KZ           | Qyzylorda/Kyzylorda/Kzyl-Orda                                             |
| Asia/Samarkand                   | +05        |  05:00     | UZ           | Uzbekistan (west)                                                         |
| Asia/Tashkent                    | +05        |  05:00     | UZ           | Uzbekistan (east)                                                         |
| Asia/Yekaterinburg               | +05        |  05:00     | RU           | MSK+02 - Urals                                                            |
| Indian/Kerguelen                 | +05        |  05:00     | TF           | Kerguelen, St Paul Island, Amsterdam Island                               |
| Indian/Maldives                  | +05        |  05:00     | MV           |                                                                           |
| Asia/Colombo                     | +0530      |  05:30     | LK           |                                                                           |
| Asia/Kolkata                     | IST        |  05:30     | IN           |                                                                           |
| Asia/Kathmandu                   | +0545      |  05:45     | NP           |                                                                           |
| Antarctica/Vostok                | +06        |  06:00     | AQ           | Vostok                                                                    |
| Asia/Almaty                      | +06        |  06:00     | KZ           | Kazakhstan (most areas)                                                   |
| Asia/Bishkek                     | +06        |  06:00     | KG           |                                                                           |
| Asia/Dhaka                       | +06        |  06:00     | BD           |                                                                           |
| Asia/Omsk                        | +06        |  06:00     | RU           | MSK+03 - Omsk                                                             |
| Asia/Qostanay                    | +06        |  06:00     | KZ           | Qostanay/Kostanay/Kustanay                                                |
| Asia/Thimphu                     | +06        |  06:00     | BT           |                                                                           |
| Asia/Urumqi                      | +06        |  06:00     | CN           | Xinjiang Time                                                             |
| Indian/Chagos                    | +06        |  06:00     | IO           |                                                                           |
| Asia/Yangon                      | +0630      |  06:30     | MM           |                                                                           |
| Indian/Cocos                     | +0630      |  06:30     | CC           |                                                                           |
| Antarctica/Davis                 | +07        |  07:00     | AQ           | Davis                                                                     |
| Asia/Bangkok                     | +07        |  07:00     | TH           | Indochina (most areas)                                                    |
| Asia/Barnaul                     | +07        |  07:00     | RU           | MSK+04 - Altai                                                            |
| Asia/Ho_Chi_Minh                 | +07        |  07:00     | VN           | Vietnam (south)                                                           |
| Asia/Hovd                        | +07        |  07:00     | MN           | Bayan-Ölgii, Govi-Altai, Hovd, Uvs, Zavkhan                               |
| Asia/Jakarta                     | WIB        |  07:00     | ID           | Java, Sumatra                                                             |
| Asia/Krasnoyarsk                 | +07        |  07:00     | RU           | MSK+04 - Krasnoyarsk area                                                 |
| Asia/Novokuznetsk                | +07        |  07:00     | RU           | MSK+04 - Kemerovo                                                         |
| Asia/Novosibirsk                 | +07        |  07:00     | RU           | MSK+04 - Novosibirsk                                                      |
| Asia/Pontianak                   | WIB        |  07:00     | ID           | Borneo (west, central)                                                    |
| Asia/Tomsk                       | +07        |  07:00     | RU           | MSK+04 - Tomsk                                                            |
| Indian/Christmas                 | +07        |  07:00     | CX           |                                                                           |
| Asia/Brunei                      | +08        |  08:00     | BN           |                                                                           |
| Asia/Choibalsan                  | +08        |  08:00     | MN           | Dornod, Sükhbaatar                                                        |
| Asia/Hong_Kong                   | HKT        |  08:00     | HK           |                                                                           |
| Asia/Irkutsk                     | +08        |  08:00     | RU           | MSK+05 - Irkutsk, Buryatia                                                |
| Asia/Kuala_Lumpur                | +08        |  08:00     | MY           | Malaysia (peninsula)                                                      |
| Asia/Kuching                     | +08        |  08:00     | MY           | Sabah, Sarawak                                                            |
| Asia/Macau                       | CST        |  08:00     | MO           |                                                                           |
| Asia/Makassar                    | WITA       |  08:00     | ID           | Borneo (east, south); Sulawesi/Celebes, Bali, Nusa Tengarra; Timor (west) |
| Asia/Manila                      | PST        |  08:00     | PH           |                                                                           |
| Asia/Shanghai                    | CST        |  08:00     | CN           | Beijing Time                                                              |
| Asia/Singapore                   | +08        |  08:00     | SG           |                                                                           |
| Asia/Taipei                      | CST        |  08:00     | TW           |                                                                           |
| Asia/Ulaanbaatar                 | +08        |  08:00     | MN           | Mongolia (most areas)                                                     |
| Australia/Perth                  | AWST       |  08:00     | AU           | Western Australia (most areas)                                            |
| Australia/Eucla                  | +0845      |  08:45     | AU           | Western Australia (Eucla)                                                 |
| Asia/Chita                       | +09        |  09:00     | RU           | MSK+06 - Zabaykalsky                                                      |
| Asia/Dili                        | +09        |  09:00     | TL           |                                                                           |
| Asia/Jayapura                    | WIT        |  09:00     | ID           | New Guinea (West Papua / Irian Jaya); Malukus/Moluccas                    |
| Asia/Khandyga                    | +09        |  09:00     | RU           | MSK+06 - Tomponsky, Ust-Maysky                                            |
| Asia/Pyongyang                   | KST        |  09:00     | KP           |                                                                           |
| Asia/Seoul                       | KST        |  09:00     | KR           |                                                                           |
| Asia/Tokyo                       | JST        |  09:00     | JP           |                                                                           |
| Asia/Yakutsk                     | +09        |  09:00     | RU           | MSK+06 - Lena River                                                       |
| Pacific/Palau                    | +09        |  09:00     | PW           |                                                                           |
| Australia/Darwin                 | ACST       |  09:30     | AU           | Northern Territory                                                        |
| Antarctica/DumontDUrville        | +10        |  10:00     | AQ           | Dumont-d'Urville                                                          |
| Asia/Ust-Nera                    | +10        |  10:00     | RU           | MSK+07 - Oymyakonsky                                                      |
| Asia/Vladivostok                 | +10        |  10:00     | RU           | MSK+07 - Amur River                                                       |
| Australia/Brisbane               | AEST       |  10:00     | AU           | Queensland (most areas)                                                   |
| Australia/Lindeman               | AEST       |  10:00     | AU           | Queensland (Whitsunday Islands)                                           |
| Pacific/Chuuk                    | +10        |  10:00     | FM           | Chuuk/Truk, Yap                                                           |
| Pacific/Guam                     | ChST       |  10:00     | GU           |                                                                           |
| Pacific/Port_Moresby             | +10        |  10:00     | PG           | Papua New Guinea (most areas)                                             |
| Asia/Magadan                     | +11        |  11:00     | RU           | MSK+08 - Magadan                                                          |
| Asia/Sakhalin                    | +11        |  11:00     | RU           | MSK+08 - Sakhalin Island                                                  |
| Asia/Srednekolymsk               | +11        |  11:00     | RU           | MSK+08 - Sakha (E); North Kuril Is                                        |
| Pacific/Bougainville             | +11        |  11:00     | PG           | Bougainville                                                              |
| Pacific/Efate                    | +11        |  11:00     | VU           |                                                                           |
| Pacific/Guadalcanal              | +11        |  11:00     | SB           |                                                                           |
| Pacific/Kosrae                   | +11        |  11:00     | FM           | Kosrae                                                                    |
| Pacific/Noumea                   | +11        |  11:00     | NC           |                                                                           |
| Pacific/Pohnpei                  | +11        |  11:00     | FM           | Pohnpei/Ponape                                                            |
| Asia/Anadyr                      | +12        |  12:00     | RU           | MSK+09 - Bering Sea                                                       |
| Asia/Kamchatka                   | +12        |  12:00     | RU           | MSK+09 - Kamchatka                                                        |
| Pacific/Funafuti                 | +12        |  12:00     | TV           |                                                                           |
| Pacific/Kwajalein                | +12        |  12:00     | MH           | Kwajalein                                                                 |
| Pacific/Majuro                   | +12        |  12:00     | MH           | Marshall Islands (most areas)                                             |
| Pacific/Nauru                    | +12        |  12:00     | NR           |                                                                           |
| Pacific/Tarawa                   | +12        |  12:00     | KI           | Gilbert Islands                                                           |
| Pacific/Wake                     | +12        |  12:00     | UM           | Wake Island                                                               |
| Pacific/Wallis                   | +12        |  12:00     | WF           |                                                                           |
| Pacific/Enderbury                | +13        |  13:00     | KI           | Phoenix Islands                                                           |
| Pacific/Fakaofo                  | +13        |  13:00     | TK           |                                                                           |
| Pacific/Tongatapu                | +13        |  13:00     | TO           |                                                                           |
| Pacific/Kiritimati               | +14        |  14:00     | KI           | Line Islands                                                              |
