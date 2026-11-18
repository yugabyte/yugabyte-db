
CREATE TABLE public.test_dump (
    nom_commune text,
    code_postal text,
    code_insee text,
    latitude numeric,
    longitude numeric,
    eloignement text
);

COPY public.test_dump (nom_commune, code_postal, code_insee, latitude, longitude, eloignement) FROM stdin;
Choëx	CH-1871	CH-1871	46.2394	6.9511	\N
Boussu	BE-7300	BE-7300	50.4006	4.2215	\N
Lille	59800	59350	50.633333	3.066667	0.89
Marseille	13012	13012	43.33756	5.43026	\N
Paris	75019	75019	48.8768285	2.3941052	0
Asnières	92600	92600	48.9124699	2.2859462	0
Attignat	01340	01024	46.283333	5.166667	1.21
Beaupont	01270	01029	46.4	5.266667	1.91
Bény	01370	01038	46.333333	5.283333	1.51
Béreyziat	01340	01040	46.366667	5.05	1.71
Bohas-Meyriat-Rignat	01250	01245	46.133333	5.4	1.01
Bourg-en-Bresse	01000	01053	46.2	5.216667	1
\.
