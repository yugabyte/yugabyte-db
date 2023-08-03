// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include <string>

namespace yb::util::testing {

// RS256
const std::string PEM_RS256_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA3dkN0LTLvH9wl+vL+MYX
tVsvyd4NS9oatGzPfJWTIUOii+N7SmMV383XHfysAm6M/DTqW3HOxzDF0hLIMXzq
UDjyQizGIZ37RkF4GqIcOSEYwkc2IWVnWl4WcSK+2KUlwMe3PpXdxtVZBFGdOVkw
bXrdsFiYU11kRhfTbz0pP3lmm84QEzCrP9Jueu1zqeyj/SBLUszNkgofp/DpTVPK
TVtkkqqNYBRF7HhPgR3G2F90NCfHMTjUQICFNP+HT+UO7XS35dmqBJNgAO7aIiok
rZhl3TrQUrknwlxBTF3gv1Zjru1YG6k/lTHVFcVN3pY+Lr2IiJUdppgpreklY7n8
jwIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_RS256_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEA3dkN0LTLvH9wl+vL+MYXtVsvyd4NS9oatGzPfJWTIUOii+N7
SmMV383XHfysAm6M/DTqW3HOxzDF0hLIMXzqUDjyQizGIZ37RkF4GqIcOSEYwkc2
IWVnWl4WcSK+2KUlwMe3PpXdxtVZBFGdOVkwbXrdsFiYU11kRhfTbz0pP3lmm84Q
EzCrP9Jueu1zqeyj/SBLUszNkgofp/DpTVPKTVtkkqqNYBRF7HhPgR3G2F90NCfH
MTjUQICFNP+HT+UO7XS35dmqBJNgAO7aIiokrZhl3TrQUrknwlxBTF3gv1Zjru1Y
G6k/lTHVFcVN3pY+Lr2IiJUdppgpreklY7n8jwIDAQABAoIBAAOkcKmgjkfF/98+
q9alyfXcTWiPEMDSD+YucymkewnsxlptnbSW8+D8zC9d2qUfk4kAhWiC8dYrYtQU
It1NI7u1c6TKf2ZF5b49jO9DAhueA34NFUJvG8dMDCpHW8LK01fa75NDeqStFA0S
Gfa7FCR4A/PFQJr9yYutEHefFXJJUCadeSdSpJ6MBhYy/wKAhr6Ua8vQbOfQZNg7
Xh3bt7Km5xj1R/lghcVc4e7vx4NbELItnO8QbzHSxFb8Xs+r8K5qp/gHYDj/dPEE
NDYqBd/aODDxYtjg9G4lZOtM5H0mpi2TNzlXyfTiRW7kWiBIVuCqI32bKcM+18Tq
F9zLqqECgYEA76HG5qqoW8QrVE8yVvIbg56O293jnTV4vbhN88D7YV72wdNQQkDL
UNN5sbmuoY2LQDg1bVk68IoJNmu0heF0iZmOJYR++iQzWuUS3I7a+xtSp3Qd2qhU
i1opGhxNCuaopKOw5ttK6PKiQm1a/PUCbWn8wDSVDjV5O7y8xeq6QmMCgYEA7QBN
o/wXEz6XuYXh19dVHMZ/vXAZjZGNa03NOcbppUZHq1h7YVR144TansYTM8Xu1myl
egoa2pxN3wYH9gBXsX7iMpL/RD+eXnQXp1yIQOrBc21/CCkDOplAsusdQdnV4EyR
5yA/GZFgY7/IShmGsR/fMvnpXs8xPuRbxZDnHuUCgYEAh31CF+QAIzqsgRPyU4S6
l9XL0ncIHjhAl4ygzqSbvbdS786J/5vhGUco9JsXKRL93Aar9rLQB3cUtGd7f4M1
QCPJYl8i6E4Vl1wUKQ7As+AEANg/lQU+IDiPKss7qGE4kzZWbIErPsEJi2OHYaUq
hTC7DvXsHUeQz3zsgz8vpx8CgYB+h9PrwdHb/2XnsZfCsX8KTtuyGuA5mcTjzfTM
bOsexufKjgHJE9ugrbQ+YkesM3dw2S57elud7ScR89laOBKZe8Ft+Nb56/E0QkzC
mH9SEUNYydOxWpwTs/A71ZSYLKGoD5kxySCHGPtaJfDbxscHV5nFUHGMoZeMGUT5
tIQAFQKBgG/VVVqcFoR/1vVjaABXPtn2kFhv20yXI8fLykMboFpuU5jGRSDoTpiU
C84GN3/pSwLdNAC9wzuvt9sFW1/oQt8i9oM0d6jGYDxGtaPNhLiVl71zCfra+GZD
UwkSSNPJlcIiN84obfQ5Doem2uak0+GqpKBForup7/dz52o5X0i/
-----END RSA PRIVATE KEY-----
)";

const std::string JWK_RS256 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"kid\":\"rs256_keyid\","
    "\"alg\":\"RS256\","
    "\"n\":\"3dkN0LTLvH9wl-vL-MYXtVsvyd4NS9oatGzPfJWTIUOii-N7SmMV383XHfysAm6M_DTqW3HOxzDF0hLIMXzqUD"
        "jyQizGIZ37RkF4GqIcOSEYwkc2IWVnWl4WcSK-2KUlwMe3PpXdxtVZBFGdOVkwbXrdsFiYU11kRhfTbz0pP3lmm84Q"
        "EzCrP9Jueu1zqeyj_SBLUszNkgofp_DpTVPKTVtkkqqNYBRF7HhPgR3G2F90NCfHMTjUQICFNP-HT-UO7XS35dmqBJ"
        "NgAO7aIiokrZhl3TrQUrknwlxBTF3gv1Zjru1YG6k_lTHVFcVN3pY-Lr2IiJUdppgpreklY7n8jw\"}";

// RS384
const std::string PEM_RS384_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqtUV/qPEXN7a2jR/E4k9
pdqy1wiRHKyQoiybOW7Nm+JMR7qa6fq6U95YyeuC6mpDEQUpnEyqLrEP8HxBZOgw
Hkln5PwUhyAS2kcsQTf0RDGG2YBeNNA+sCb4+oM5O0NwWt0pJIoFPNIyOxRYdSZB
A3h5MvwIgQPbj4+a+YSjQsborfEywcqozHUDQ4VFadoO9tIIVaPIRqANs54BokCf
OyduP+dqlf2d3q1yukFQ2K7L27mrDtCXcWjS5CJW+oGf/CyOSj+yyaNug7sOlvU5
AwjG3l7EZ0GRFeROWl5pj6Hf054o4WI2m3xXY8S38hO6jb/NvlG0pg4ZHZEvvqCM
dQIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_RS384_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEAqtUV/qPEXN7a2jR/E4k9pdqy1wiRHKyQoiybOW7Nm+JMR7qa
6fq6U95YyeuC6mpDEQUpnEyqLrEP8HxBZOgwHkln5PwUhyAS2kcsQTf0RDGG2YBe
NNA+sCb4+oM5O0NwWt0pJIoFPNIyOxRYdSZBA3h5MvwIgQPbj4+a+YSjQsborfEy
wcqozHUDQ4VFadoO9tIIVaPIRqANs54BokCfOyduP+dqlf2d3q1yukFQ2K7L27mr
DtCXcWjS5CJW+oGf/CyOSj+yyaNug7sOlvU5AwjG3l7EZ0GRFeROWl5pj6Hf054o
4WI2m3xXY8S38hO6jb/NvlG0pg4ZHZEvvqCMdQIDAQABAoIBAQCT8X3ezIzdsNHv
bs8uaAhPfhqrRuwE3R1UlTTIhEDj4xMUe3J1d4Gt6D0UgTUbNXNnZgUnKu2nCgg3
yCQJ81rYn9Gt6PEOJKSvDDwzLvYHqKyT6CutqTrg6p9ss//4ZusChc1/q1fl2FNP
/sqsibh7/PVZRhNHR8P5i/A2brvEs3VjXKGw0eOJjUn6dA6mzqs06qsgjqhNI323
iND/PD57ZG5lSIOZM/NhmuJ89XixNUro3RJGzLE6owHa9p6RWh9Gzrl9FxPlzzSi
kmQyVAoCCKUZ99eYTEMQ5R5ZOwNjs821TyOKjQfdJnTrrp7GzQNoY7XwXX+IamuS
+jtMDVlxAoGBAPxh/ulHnhs6rzb3AR1a4y1NKXX+a3b2N1+KsgxG/sDBaesgQcro
5fkA1YVHv95elZu/gB9uHgXly/Rly+NL7TAor7I8nGd+GQTEIbbZ0irXU7OPIQWY
v8Z2UnDd8DJv5T6uoKoN8ri1EojVrVj5uC1CcVBbiVxeI4y+HEvuG7SnAoGBAK1H
4LouEvxVKlS8GQRpjEwmOyVVY8Jd+j/fY3eaPKELLtOsj6Ban63c0qinPjb2PMjY
Z4nqD8GO/E9R+E0SZ2aaNoMNXVu6O7hi2UWH8pe29nIub7Jt2xpdSDh2RaLATCqa
QqWpvM8+A/e3stQ82LfnRFJRpSUkDsJLZ1R6OG2DAoGBALAq7zaCyTgUhI2HaP3G
nWDXxaMZToYhY5GLTLEJNXXzDC4VvBcY7r4a+PApnyJnP2MSDyrhQI+5Ud5s2B72
tr+xBsMRT9Nlz6zmAuqRrQQ+fayOsewoLWUo3m7uXGW4eXqBhqBtUAniSue8z12W
Ihtlj5cZ7g3NoF7zrOjLcgdtAoGBAKXmEDEQFZtCipGvuJ/x0aHCZJQcybL4OLRY
UqnaoDtrMnz0VFopCYHyzjksTbNfUtjT32U6E7W0CLqEdx6LBTZFZPVZoU1F4xFo
ii44tzkrsY2mCcihxsjaEGAGVCs6wnFzWWW0OZGNBU1wsaTjUHXZ1B6gDmWdvQem
G5rUnRuzAoGAeUEHuDJ4lWBfFKg5xcj1pD2k5cuOi2SEROWd74B+tSd6+zKTTOmA
aZvRTtyYq9wwYaVFMcbDpufgbstu+xToBRDpSRWeYW5iqiHqBemcOwP4PgBZeDa5
nPLvydzhsXY5r8SHFuFexphbOMAMypC5Bg4DZTfrLJu0D3XnH0vT7Nw=
-----END RSA PRIVATE KEY-----
)";
const std::string JWK_RS384 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"use\":\"sig\","
    "\"kid\":\"rs384_keyid\","
    "\"alg\":\"RS384\","
    "\"n\":\"qtUV_qPEXN7a2jR_E4k9pdqy1wiRHKyQoiybOW7Nm-JMR7qa6fq6U95YyeuC6mpDEQUpnEyqLrEP8HxBZOgwHk"
        "ln5PwUhyAS2kcsQTf0RDGG2YBeNNA-sCb4-oM5O0NwWt0pJIoFPNIyOxRYdSZBA3h5MvwIgQPbj4-a-YSjQsborfEy"
        "wcqozHUDQ4VFadoO9tIIVaPIRqANs54BokCfOyduP-dqlf2d3q1yukFQ2K7L27mrDtCXcWjS5CJW-oGf_CyOSj-yya"
        "Nug7sOlvU5AwjG3l7EZ0GRFeROWl5pj6Hf054o4WI2m3xXY8S38hO6jb_NvlG0pg4ZHZEvvqCMdQ\"}";

// RS512
const std::string PEM_RS512_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoF6oCRUztdPc2/jCuuD0
aKlv+baqMmymiYYwU1Gf+UZ4dNUI8gxDLHZW7tS0uYYTn85r8UJ3DeSskT41FdAZ
b1Bfi21fSqAhT169P+hB8RYIyVQNubyJsFXK1xfRp7H3tlO60C1lrv9YLNNXXxQn
hGdxAAXQeAsecEUQ/GQS1PdS22vl95cn42051pLf4/ssmReZZmRz3htlJDIumVMM
P6FJLbxvBgdODCdUappMwkI/com2Orz4sYFk8GYvsJC4o/hsE9AQU8OTm0z9Jxmy
WDu6FE/BOr9UryKwsqon/2K4ufAqU5ePvmiEv0goqInC9DU7fGFLc8shv4S3fY8w
vwIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_RS512_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEAoF6oCRUztdPc2/jCuuD0aKlv+baqMmymiYYwU1Gf+UZ4dNUI
8gxDLHZW7tS0uYYTn85r8UJ3DeSskT41FdAZb1Bfi21fSqAhT169P+hB8RYIyVQN
ubyJsFXK1xfRp7H3tlO60C1lrv9YLNNXXxQnhGdxAAXQeAsecEUQ/GQS1PdS22vl
95cn42051pLf4/ssmReZZmRz3htlJDIumVMMP6FJLbxvBgdODCdUappMwkI/com2
Orz4sYFk8GYvsJC4o/hsE9AQU8OTm0z9JxmyWDu6FE/BOr9UryKwsqon/2K4ufAq
U5ePvmiEv0goqInC9DU7fGFLc8shv4S3fY8wvwIDAQABAoIBAQCT8t+ZCYtcKumt
slCMMa6pw+8+9AsOW/hEFZ0NsNciFKZaOpN3ImLyaPaIfYmBQrVmD/y7ZfMJyTZ/
BGHbDtH4RLDwo2VvJk20uJVlmPME5KwUeMv014A7QtrQFvRffismdRZ6qfcOPBnv
uMX5PFG8r+Wq/LI3nSJmtwEVp4lMGKqKUypCH0Fyq++9fh+lHaaAH+W6ZYoOHjw4
NadPlk13A3I9Iq6zA9QhrUtNdGoXDUlg2yltQG2fd2lPT4gz6Xlv2naL83JmRluO
EZWsq8C7y5w0/hNIarf+7BrXiIDdsnAzhlVh+7Ix4+Ojx6EnLPAh6I5lGYIhmul8
0E8mT9hRAoGBAOevjpDOGI3omAJR49fRf9B2VI/VLcJb5pO5Ah6PNK/TaiOG0k6J
DpasseITvXnwcYdrPmZBkA35i4QUwnDxn+97/nOovfejWKt40FXWBPBClumE4rfz
dHkAUEGGIaLlIQGOGn+h9WT5KMhYoAPGrNRuheubyrGTNs6sT41E5y3XAoGBALEz
IbC3LvWQf9rfpW2M+WC5/rfWt0CkCBZTzZu9Uk4faa9EczmCSqeXEoQkAwewcuA8
N/T7+Y6ugWgfb/Az8ZsP3iIsbCaYX3vad8s72oL9SItfOBdqB3yxDY+Lonx2zhyC
20XtuVXjcTWll4M+ucUp8N9AMx5+fcKqs54U36dZAoGBAMOmzY4bfUDZmwTagr5O
fNFeHCsaq3nmgeFd6xxDcwrITmmSASexNlCnpdB1Ox0un7DsL9XKqAwlIFx563nV
kmp7G3Ywmbv2hXrIm6bhBWqf0TGCtrMBNOq6CQxMaTtWo3jcuCPwcXrDrl0B+p81
t93tN8qv1Yv/9diySrvR5CghAoGAQdDyFIcVpBQVySAEe9o+zhSHbZUM36+NaW2b
EtuQ9H9qa7UK7zNbsz/Dmt0dWv/Iy0zSo+XrXXmnixsSIq/Ib4XHRf4l9XfnD0On
9w62LK0TAuFNHjU9rqy8krKUmZIvIBvigei4TBR8eiaVTiRAL+FSHDnmQs9Mur9Y
k8DBCZECgYEAhlzn7sM3cuzD20UWRp6OzB2nxsekXDnv94uyQRb1x4k07Z3+YgPH
HIYu8ckjcnNX8+FdAbDR3P16Kisx9DMeswoecXp3IMgSS00Z+MNWkvNiaEtaW54Z
1jiuIyS4aNVodleE1P94FutrAtjwf40eeEcF+uOAqfLLLfXNiKmhuGQ=
-----END RSA PRIVATE KEY-----
)";
const std::string JWK_RS512 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"use\":\"sig\","
    "\"kid\":\"rs512_keyid\","
    "\"alg\":\"RS512\","
    "\"n\":\"oF6oCRUztdPc2_jCuuD0aKlv-baqMmymiYYwU1Gf-UZ4dNUI8gxDLHZW7tS0uYYTn85r8UJ3DeSskT41FdAZb1"
        "Bfi21fSqAhT169P-hB8RYIyVQNubyJsFXK1xfRp7H3tlO60C1lrv9YLNNXXxQnhGdxAAXQeAsecEUQ_GQS1PdS22vl"
        "95cn42051pLf4_ssmReZZmRz3htlJDIumVMMP6FJLbxvBgdODCdUappMwkI_com2Orz4sYFk8GYvsJC4o_hsE9AQU8"
        "OTm0z9JxmyWDu6FE_BOr9UryKwsqon_2K4ufAqU5ePvmiEv0goqInC9DU7fGFLc8shv4S3fY8wvw\"}";

// PS256
const std::string PEM_PS256_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAo2vAJwdvkyIICvLRvvGn
0rEsoSkQHFyPIVoELqKXNxBke8Xkn+yxe1zWx62d71h6ewIpPPHZ7K5lMZJbj+Xq
JIy+g2/7ZWujQhmE/v1JY3TNkAGNsKJupBWPqsyUzEou+ApERvj+IHASQ+zinitD
UxhFPb+1q32T1gRgzU2YWVDTa7StgPxu5cu+GuCeh5uc5FCmCYvtD2TOzWWbh8qi
ESmYTFR/4n6gQyt/v16iRJCDuR7/HUG0LlgDf78AJDQOZllPXG8Kcj1/Y1lOosUW
g0hjMIS6KqB2nQ+PiLoc9QqOklfHDy2JHrOFb5P1S6vmK75JL3kdd1EyxkgVOWAE
iQIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_PS256_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAo2vAJwdvkyIICvLRvvGn0rEsoSkQHFyPIVoELqKXNxBke8Xk
n+yxe1zWx62d71h6ewIpPPHZ7K5lMZJbj+XqJIy+g2/7ZWujQhmE/v1JY3TNkAGN
sKJupBWPqsyUzEou+ApERvj+IHASQ+zinitDUxhFPb+1q32T1gRgzU2YWVDTa7St
gPxu5cu+GuCeh5uc5FCmCYvtD2TOzWWbh8qiESmYTFR/4n6gQyt/v16iRJCDuR7/
HUG0LlgDf78AJDQOZllPXG8Kcj1/Y1lOosUWg0hjMIS6KqB2nQ+PiLoc9QqOklfH
Dy2JHrOFb5P1S6vmK75JL3kdd1EyxkgVOWAEiQIDAQABAoIBAQCJ6glD5taWiQXY
l4vDZRWIjdVoPMtH5CU2tE0LPlP3OHJUsnF7NbmirnrkEPVUZIsY/H3o3QJY5+Sm
rSlwi0vKhKzTJ9I1iV1CD19aAk/JC23fti/pfWt6NmgEcJqyvXheA+wTKVbt8Sa5
BFVLvp8WpUjqD7w1eckluJQpLu7/kAfhDbbM0rj6srrywfqZ+mRgfxiQ/N0O6CN2
Hnsi7gBVw9cEfKIhJzesaCEc6jYTFzhbWN2cvCFfT8aJ8u0N+J9ARwnMk7jqQ6pI
AnSe/ALG9MbhAWeVbJMDyLvH1qvL8dwcz6cXBDnjLgnI+D0wLjqUiR56cKTqP9V8
1PM2NozBAoGBAOCEtMHn0dMTWGgS8Kc5Q3kCAdE3hdVok6lawLvXJ7itCHn5sBqu
gZu+WLJilRib8p8MqaMQzTICuWsgjTuQrtPiXzYNN6NKsguax2UPTUBJoelenHXO
pa5yoAtLvtH4Nan8OFcC3/TqCrKfpNyM5CZ6TG6SuCWF5oeasYNsGf3NAoGBALpV
5OiIG4ePhZ7qa8Qmkg+nHpicYXO08CDreqxl9k3tCxUjW5WN/+bvNWwCJLUy4IeN
Z2nomlxihziplDIRHXL7CHma6kh8OjVHr+cpmWn5xv9R3BTuOZS/1I1c+1mjlYR7
2hB2vv6cfTellPoCXGOSGeCrKYEfQU73CGDCxIWtAoGAZsq7S0/MlBv2TOfnAFjK
WHufw17tSlCv0ki3lwihqf6ms9mqU/zzYA/c4gcahgLYKROOExddKvluVOq5Xr0W
HfI1bzTL9Vn8fC2n/s/rqXRMyeDEN3eeCWl3dtR+D/nY7/OHA+dQC/yfWzqWK1fi
GO/DUJih8KQGcK1Vensixz0CgYBmlIjLVrrJG0L1ZJplRtKcGWWnoFep6k9T4C8N
n6hD6B50yZ1OrPjXOpNPXbK1qkefeEIZNPtdpsRIdlrmYTO0K+zTfWxC8VjeIhP9
j5IsnFxoDLm7MBa1BBJQrIKXK45RfBllfOnSo3Xv35EvPYN9MV5bp/7WXc2HWknb
cv3blQKBgBhfC+kWbFMtGwufLBQkElxIlUFrlFt/L86J2/yymH+t+x67r6fh/L2G
zYo8pyAJV3HBH+/0t7ZucY4KplazPIpRcR78CC3ws7qzFKrEYkgMggaffRXWNZ1o
aqV9+CDcF+WasYXnyva1776OjCZk33b0MQxjKghTRdM/cril3ufQ
-----END RSA PRIVATE KEY-----
)";
const std::string JWK_PS256 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"kid\":\"ps256_keyid\","
    "\"alg\":\"PS256\","
    "\"n\":\"o2vAJwdvkyIICvLRvvGn0rEsoSkQHFyPIVoELqKXNxBke8Xkn-yxe1zWx62d71h6ewIpPPHZ7K5lMZJbj-XqJI"
        "y-g2_7ZWujQhmE_v1JY3TNkAGNsKJupBWPqsyUzEou-ApERvj-IHASQ-zinitDUxhFPb-1q32T1gRgzU2YWVDTa7St"
        "gPxu5cu-GuCeh5uc5FCmCYvtD2TOzWWbh8qiESmYTFR_4n6gQyt_v16iRJCDuR7_HUG0LlgDf78AJDQOZllPXG8Kcj"
        "1_Y1lOosUWg0hjMIS6KqB2nQ-PiLoc9QqOklfHDy2JHrOFb5P1S6vmK75JL3kdd1EyxkgVOWAEiQ\"}";

// PS384
const std::string PEM_PS384_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA+an4opwpewibqEzJ3nzo
yjT6fJrXxG8cwLaUy1HYs/CfsuD3+sV/XAXxvY6x8n6P+VBNRY7XUCfL+Z4qodLa
j2rmzGw6Drjxpj+4EDqf4OsTdIe00qfYcdUvuEcuDtnKhE+oHVuGd/nFn4sFjsa7
rMpvqn9JVTe0farJ8w2oAxh8SAE3bv0WsazCSKR9d8StgqJc24RfiylP/pDm36y5
Tp/VgLxlJDuH/3/27BcNvcts7P7ZFxQ2lbJBYSsouI7n9bryLX6FXhBYZOoBwXjz
jvsjU/mpbIlI/CCoP0CJps/XRa4yIG1vQf9zKdnULje+OnCuPJa+sb43XPDzQuq+
iQIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_PS384_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEogIBAAKCAQEA+an4opwpewibqEzJ3nzoyjT6fJrXxG8cwLaUy1HYs/CfsuD3
+sV/XAXxvY6x8n6P+VBNRY7XUCfL+Z4qodLaj2rmzGw6Drjxpj+4EDqf4OsTdIe0
0qfYcdUvuEcuDtnKhE+oHVuGd/nFn4sFjsa7rMpvqn9JVTe0farJ8w2oAxh8SAE3
bv0WsazCSKR9d8StgqJc24RfiylP/pDm36y5Tp/VgLxlJDuH/3/27BcNvcts7P7Z
FxQ2lbJBYSsouI7n9bryLX6FXhBYZOoBwXjzjvsjU/mpbIlI/CCoP0CJps/XRa4y
IG1vQf9zKdnULje+OnCuPJa+sb43XPDzQuq+iQIDAQABAoIBACqRRmlDQvdNQmzx
msGqfm5rlgHEzOEbKMXa24fHDrGvZysynFVPcyWXhd4SBFkaJyYVAPfJIQDzqB2J
UC+DKeymB2/3S/bSpbD40ENEG5pXult1/+ii4Y6XEKnUyQ+TJqpkgn9u6YBvP7iy
FWKAk+MkKOHjDffPpaB/jCs6uti7785XOl0zZ4pl3L221+bxMW8TwnNS+1CyrpZp
wi12QASjoz2ul1tHY9R16mqfXEjrJysLU5PPjX7T4jkXZd4c0ZbkzonvQA0TRT63
If5SnPPYj3ezAOIznG2mn/wnUsdbMdnNTItrT0tZQ7VZxHEMIVDGM4enincdqd4h
HzqDDwECgYEA/tRb9hooIsgjbWf3XKXQwf1FoaTWwuEthzqB06nixgK6advNCPJ+
FAhUjEjOI2pNKyWKtr+KuGC0aEROLcEFd+HGbIytCgXSqPdrrinMxflo3C55MPDL
CX/C1tiLpl4qS5dLu91GG2/Ao9iqJjjPX/VUwBBqyNZb4AMUwhITx3ECgYEA+s+J
vyUjSgOHyxrJURL+OJXI1UK99I4royr20od6MENtX4RztlOVnAQkhfr9nHhwATaa
bQyj5s6NA3l5tfjlUKbNIRSoYYpK64tzlphBHJ6/Ju26MPm9Fon/NOfkD85OXfmk
QKK9JQX2zSZDqX7PjEyLCdn9ogkzrFpjJhCTTJkCgYAeqNQrpvf/P08r4Z9cUULt
pBhVm2yPY+JDa+Gk4sK7Cib9h4mCsxJCPMJXYocSsb55G3o2BJOfHVt3VAiH7rpG
sy5Zbw9+rjarR6F4AeV2SEy6eQjxv3bePLXnfYRHzvqNv7VH9BZ0RJzL2cyW7qzc
obrBpPgEE+5X5GcB9oTUsQKBgDptTHZ1zVG5ntGmrM0xMn22DvH3OU8WK344TQVg
QAusOXdt4JLRx+FvaZ64iIoB2H2/3ZuXvTrQVTNBAiRtFeaC5PhW2p7WW3uhocys
LUbgiEEmUiCEnRL6FLSbbJpuAf2MYUIZZxyP1h2WU17RxsG1NxKVcKtln18AM2az
p8zJAoGAfnge7bcLqmt6Q8oXlYjQHPTHmUlOgfYebpNrxa3jSjOmtGkPLUQ0GGD1
Lf8EuPuFSRH7v05THU4mhUFzQSRO9pPOcAMuQqm2bo9UfM9wYxe7jD6V2lrcqaYV
oaeW3Z1XGBYjtYJyR0Pogg7umSBeHD37zSELB8HYebCL3SuE184=
-----END RSA PRIVATE KEY-----
)";
const std::string JWK_PS384 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"use\":\"sig\","
    "\"kid\":\"ps384_keyid\","
    "\"alg\":\"PS384\","
    "\"n\":\"-an4opwpewibqEzJ3nzoyjT6fJrXxG8cwLaUy1HYs_CfsuD3-sV_XAXxvY6x8n6P-VBNRY7XUCfL-Z4qodLaj2"
        "rmzGw6Drjxpj-4EDqf4OsTdIe00qfYcdUvuEcuDtnKhE-oHVuGd_nFn4sFjsa7rMpvqn9JVTe0farJ8w2oAxh8SAE3"
        "bv0WsazCSKR9d8StgqJc24RfiylP_pDm36y5Tp_VgLxlJDuH_3_27BcNvcts7P7ZFxQ2lbJBYSsouI7n9bryLX6FXh"
        "BYZOoBwXjzjvsjU_mpbIlI_CCoP0CJps_XRa4yIG1vQf9zKdnULje-OnCuPJa-sb43XPDzQuq-iQ\"}";

// PS512
const std::string PEM_PS512_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAhcPDlRPlk7J/Ch6CcPu9
3L5TsESmYcnJbSfKhobOqpKmoQGDVB6wE+Lel5nZzB23+rPpBa0tUPeWHn0g4vJ9
vGjZQMDYvJJ3q5QPkO+KGqigNAMGQkv4tc2y6tdutuZ9q6ftlsysDuooKrtpNDMH
OhUIIVf8W9aaMrJa5PwnjoLS0eaZQVKi/uMYY7gdICjKP173GYJg733Tvv0rVTZs
RvFgj5EQRx2e8vfFxJFtXGUcAEpEViYKwWjjvwJDlDsI/zjiggAgoIQfZJxqRuyY
b5FAuIUJyd9fNM67YzidYrS3JzoBftrOWU2trO5lqpt0VE+Y98UzLslnGEXiBrcX
rQIDAQAB
-----END PUBLIC KEY-----
)";
const std::string PEM_PS512_PRIVATE = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEogIBAAKCAQEAhcPDlRPlk7J/Ch6CcPu93L5TsESmYcnJbSfKhobOqpKmoQGD
VB6wE+Lel5nZzB23+rPpBa0tUPeWHn0g4vJ9vGjZQMDYvJJ3q5QPkO+KGqigNAMG
Qkv4tc2y6tdutuZ9q6ftlsysDuooKrtpNDMHOhUIIVf8W9aaMrJa5PwnjoLS0eaZ
QVKi/uMYY7gdICjKP173GYJg733Tvv0rVTZsRvFgj5EQRx2e8vfFxJFtXGUcAEpE
ViYKwWjjvwJDlDsI/zjiggAgoIQfZJxqRuyYb5FAuIUJyd9fNM67YzidYrS3JzoB
ftrOWU2trO5lqpt0VE+Y98UzLslnGEXiBrcXrQIDAQABAoIBAHNCKYaM7GaFiT2Y
6GCeKgzI2qepn4vnKW6quLGN+wmy720QNq8G+kVIWPBcGvTsLpkQ6JqBi+iWTX3b
57hlpb3wwjIveRGTSxZGr9r87Azoe5IVgREjERzmL2J3WuiyVlrQicJEfYUkcpPP
hGj8ByAe+zBv9fzUP22rjPJ96z+5bNAuOWfIvVVZoM9ywJCXdWDYUxhEtBHb5BHF
24uXiCfwmY4IIhIoqG7qNwLwR4mcu33pdKkoIeyWm4pvm/v9ymnQz9WO3nqAsf3f
vL1dreye8zlhenGJGYFV1yOngRDqdompFE8CdCmrA3o6VkGobC5p9PdA1Aney2HQ
0Gxq1UECgYEA0R7yyn2N3/Re65/3C2I9U7rWjE64WcFpXOnCB9QIyfN+MTaHdHcu
Y1S0Ow9wfLLlCDlozGBuRz23KpGpzxEk0wnftw+4xRldDf9Dep+QQ91Ra8zu0KSk
5W7JbQNmci+hkWBwBmNkVfvkA1Qhf4LGUABbcgo+7y1nX3SIrl/cXVkCgYEAo8BE
ST9gBmzCZjmUJwrsdd9iF5xp4fC1C8GJ0GayzHrQe/WFrYALQWxB+T6HktuewREk
bI0TIjamzBzkqV+Cgus5aRJNy3W/iEkmqH4ozqK/M8jGvYGoNLpov4xDaMnB5x2W
OltKEJ9ZmAtl9j/GWtC9KjtRdstSuC9P2LKQHnUCgYBSJJT8IytqkCQE7BXvW8x5
KkgCXx2c7BNGEvBLgOde5I1qKWA1yGbpT6loFZ167g53F43p0esfgseDKiqIktRj
LVq6HqvWiCr8R4urDChv7+x+qsYYIMRA9y6Z6p8ANWOCpl36tGoCOGqNQCvUWXsq
i6lf91RXQP99CFp0HGWEKQKBgAm9U9JvfIylR2LBJfi0g5/3K2RwHzQbFwmd9053
7UaQP3o//jV1mjXH9JyYhYPMoEKnfF0gfvTX/0/AoDEaxy52QzHmrs3dMQkwIKaA
6nhv4aN426lF3vVT6QsLkq7W4TuX3OmXqG4YrEGI3AWrgWbBZ57tbEj+iur9lWg0
JrJJAoGAGdfygPsrPbzWt8/HXTFmgyXSvg0lSarYJ3+sLXwq3g5O69ZAo1SrWbE9
i68KM2bwQtQyNqPlGJRbhTAwCqes6zdpZH1eSGxz4i3WGnTXJ9UYH+AX1lyyuxy2
3Z2oID2FZgPpZHuZ0CCdvAl94cQXKpDAfDKZgqdUu+4DC6zlAPw=
-----END RSA PRIVATE KEY-----
)";
const std::string JWK_PS512 =
    "{\"kty\":\"RSA\","
    "\"e\":\"AQAB\","
    "\"use\":\"sig\","
    "\"kid\":\"ps512_keyid\","
    "\"alg\":\"PS512\","
    "\"n\":\"hcPDlRPlk7J_Ch6CcPu93L5TsESmYcnJbSfKhobOqpKmoQGDVB6wE-Lel5nZzB23-rPpBa0tUPeWHn0g4vJ9vG"
        "jZQMDYvJJ3q5QPkO-KGqigNAMGQkv4tc2y6tdutuZ9q6ftlsysDuooKrtpNDMHOhUIIVf8W9aaMrJa5PwnjoLS0eaZ"
        "QVKi_uMYY7gdICjKP173GYJg733Tvv0rVTZsRvFgj5EQRx2e8vfFxJFtXGUcAEpEViYKwWjjvwJDlDsI_zjiggAgoI"
        "QfZJxqRuyYb5FAuIUJyd9fNM67YzidYrS3JzoBftrOWU2trO5lqpt0VE-Y98UzLslnGEXiBrcXrQ\"}";

// ES256
const std::string PEM_ES256_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAErOWQ2GjcxRosI46VL16zTL3Vlti3
ri0yLU6J6H0Nw+3qgvBXnN8SauSZkgGNq33Z6cHcX7DL37u6jLAB0ZFzYQ==
-----END PUBLIC KEY-----
)";
const std::string PEM_ES256_PRIVATE = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgzapLxXUHHWO5eI1I
OhzYIX0kD9WdWz4jo+tXK6h2xxuhRANCAASs5ZDYaNzFGiwjjpUvXrNMvdWW2Leu
LTItTonofQ3D7eqC8Fec3xJq5JmSAY2rfdnpwdxfsMvfu7qMsAHRkXNh
-----END PRIVATE KEY-----
)";
const std::string JWK_ES256 = R"(
{
  "kty": "EC",
  "d": "zapLxXUHHWO5eI1IOhzYIX0kD9WdWz4jo-tXK6h2xxs",
  "crv": "P-256",
  "kid": "es256_keyid",
  "x": "rOWQ2GjcxRosI46VL16zTL3Vlti3ri0yLU6J6H0Nw-0",
  "y": "6oLwV5zfEmrkmZIBjat92enB3F-wy9-7uoywAdGRc2E",
  "alg": "ES256"
})";

// ES384
const std::string PEM_ES384_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEptnqg7gKUWKg5+gD2b1KMV5LiufPsimw
abFRu9ENCMydclBe+/28iPpBzisy8iTzKsnRa6QVJPb8BCOjPGVTX48cOeoT6Brd
qYtzK1IiA8L7MIwngxTVLKptlizEPMDS
-----END PUBLIC KEY-----
)";
const std::string PEM_ES384_PRIVATE = R"(-----BEGIN PRIVATE KEY-----
MIG2AgEAMBAGByqGSM49AgEGBSuBBAAiBIGeMIGbAgEBBDB0qb3YFVeWNvKxgMN3
vfWJN1SPMx/NGkuRnVJoZvFgiF2Ab9hiRYhBcw7Q2okVxM2hZANiAASm2eqDuApR
YqDn6APZvUoxXkuK58+yKbBpsVG70Q0IzJ1yUF77/byI+kHOKzLyJPMqydFrpBUk
9vwEI6M8ZVNfjxw56hPoGt2pi3MrUiIDwvswjCeDFNUsqm2WLMQ8wNI=
-----END PRIVATE KEY-----
)";
const std::string JWK_ES384 = R"(
{
  "kty": "EC",
  "d": "dKm92BVXljbysYDDd731iTdUjzMfzRpLkZ1SaGbxYIhdgG_YYkWIQXMO0NqJFcTN",
  "crv": "P-384",
  "kid": "es384_keyid",
  "x": "ptnqg7gKUWKg5-gD2b1KMV5LiufPsimwabFRu9ENCMydclBe-_28iPpBzisy8iTz",
  "y": "KsnRa6QVJPb8BCOjPGVTX48cOeoT6BrdqYtzK1IiA8L7MIwngxTVLKptlizEPMDS",
  "alg": "ES384"
})";

// ES512
const std::string PEM_ES512_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MIGbMBAGByqGSM49AgEGBSuBBAAjA4GGAAQBcb2qErMKuZvR//y28LiXXKXZYbrS
pYKCn8497TlZH71md+94kxjPJgXzE695J6wxYQrNbGeNtqLeRS7DGwPnZ6kBn+U/
+utnHxnqzEihoWVuC8eoeAXFYQaJwFtc2h+BfC464coqyrspI1KaHaGNPOg9U4Az
WxeYwL+eFCCa0oJmcuM=
-----END PUBLIC KEY-----
)";
const std::string PEM_ES512_PRIVATE = R"(-----BEGIN PRIVATE KEY-----
MIHuAgEAMBAGByqGSM49AgEGBSuBBAAjBIHWMIHTAgEBBEIAeG/xYCQ2BkClKfej
7EkzBsvVIPxd5RRkmtDB58UVzsraIpVt4WrZJPQ/dLBFwElZIK/VwUC4o1alnzYb
lqrHPN6hgYkDgYYABAFxvaoSswq5m9H//LbwuJdcpdlhutKlgoKfzj3tOVkfvWZ3
73iTGM8mBfMTr3knrDFhCs1sZ422ot5FLsMbA+dnqQGf5T/662cfGerMSKGhZW4L
x6h4BcVhBonAW1zaH4F8LjrhyirKuykjUpodoY086D1TgDNbF5jAv54UIJrSgmZy
4w==
-----END PRIVATE KEY-----
)";
const std::string JWK_ES512 = R"(
{
    "kty": "EC",
    "d": "AHhv8WAkNgZApSn3o-xJMwbL1SD8XeUUZJrQwefFFc7K2iKVbeFq2ST0P3SwRcBJWSCv1cFAuKNWpZ82G5aqxzze",
    "crv": "P-521",
    "kid": "es512_keyid",
    "x": "AXG9qhKzCrmb0f_8tvC4l1yl2WG60qWCgp_OPe05WR-9ZnfveJMYzyYF8xOveSesMWEKzWxnjbai3kUuwxsD52ep",
    "y": "AZ_lP_rrZx8Z6sxIoaFlbgvHqHgFxWEGicBbXNofgXwuOuHKKsq7KSNSmh2hjTzoPVOAM1sXmMC_nhQgmtKCZnLj",
    "alg": "ES512"
})";

// ES256K
const std::string PEM_ES256K_PUBLIC = R"(-----BEGIN PUBLIC KEY-----
MFYwEAYHKoZIzj0CAQYFK4EEAAoDQgAEIMlyAhf2nqY1gTtVuQhHjr8sJGTR9UAl
qEyIApUNZKxG/fZx0KpagOFxYT596nzEyDZJHeGqLhN6vQn0mT9Vvw==
-----END PUBLIC KEY-----
)";
const std::string PEM_ES256K_PRIVATE = R"(-----BEGIN PRIVATE KEY-----
MIGEAgEAMBAGByqGSM49AgEGBSuBBAAKBG0wawIBAQQgN8JUNzTt+alwdQjCCDzd
oGMebq1OaQl7OZpsUbx0x9ahRANCAAQgyXICF/aepjWBO1W5CEeOvywkZNH1QCWo
TIgClQ1krEb99nHQqlqA4XFhPn3qfMTINkkd4aouE3q9CfSZP1W/
-----END PRIVATE KEY-----
)";
const std::string JWK_ES256K = R"(
{
  "kty": "EC",
  "d": "N8JUNzTt-alwdQjCCDzdoGMebq1OaQl7OZpsUbx0x9Y",
  "crv": "secp256k1",
  "kid": "es256k_keyid",
  "x": "IMlyAhf2nqY1gTtVuQhHjr8sJGTR9UAlqEyIApUNZKw",
  "y": "Rv32cdCqWoDhcWE-fep8xMg2SR3hqi4Ter0J9Jk_Vb8",
  "alg": "ES256K"
})";

const std::string JWKS =
    "{\"keys\":["
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"use\":\"sig\",\"kid\":\"rs384_keyid\",\"alg\":\"RS384\","
            "\"n\":\"qtUV_qPEXN7a2jR_E4k9pdqy1wiRHKyQoiybOW7Nm-JMR7qa6fq6U95YyeuC6mpDEQUpnEyqLrEP8H"
            "xBZOgwHkln5PwUhyAS2kcsQTf0RDGG2YBeNNA-sCb4-oM5O0NwWt0pJIoFPNIyOxRYdSZBA3h5MvwIgQPbj4-a"
            "-YSjQsborfEywcqozHUDQ4VFadoO9tIIVaPIRqANs54BokCfOyduP-dqlf2d3q1yukFQ2K7L27mrDtCXcWjS5C"
            "JW-oGf_CyOSj-yyaNug7sOlvU5AwjG3l7EZ0GRFeROWl5pj6Hf054o4WI2m3xXY8S38hO6jb_NvlG0pg4ZHZEv"
            "vqCMdQ\"},"
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"use\":\"sig\",\"kid\":\"rs512_keyid\",\"alg\":\"RS512\","
            "\"n\":\"oF6oCRUztdPc2_jCuuD0aKlv-baqMmymiYYwU1Gf-UZ4dNUI8gxDLHZW7tS0uYYTn85r8UJ3DeSskT"
            "41FdAZb1Bfi21fSqAhT169P-hB8RYIyVQNubyJsFXK1xfRp7H3tlO60C1lrv9YLNNXXxQnhGdxAAXQeAsecEUQ"
            "_GQS1PdS22vl95cn42051pLf4_ssmReZZmRz3htlJDIumVMMP6FJLbxvBgdODCdUappMwkI_com2Orz4sYFk8G"
            "YvsJC4o_hsE9AQU8OTm0z9JxmyWDu6FE_BOr9UryKwsqon_2K4ufAqU5ePvmiEv0goqInC9DU7fGFLc8shv4S3"
            "fY8wvw\"},"
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"use\":\"sig\",\"kid\":\"ps384_keyid\",\"alg\":\"PS384\","
            "\"n\":\"-an4opwpewibqEzJ3nzoyjT6fJrXxG8cwLaUy1HYs_CfsuD3-sV_XAXxvY6x8n6P-VBNRY7XUCfL-Z"
            "4qodLaj2rmzGw6Drjxpj-4EDqf4OsTdIe00qfYcdUvuEcuDtnKhE-oHVuGd_nFn4sFjsa7rMpvqn9JVTe0farJ"
            "8w2oAxh8SAE3bv0WsazCSKR9d8StgqJc24RfiylP_pDm36y5Tp_VgLxlJDuH_3_27BcNvcts7P7ZFxQ2lbJBYS"
            "souI7n9bryLX6FXhBYZOoBwXjzjvsjU_mpbIlI_CCoP0CJps_XRa4yIG1vQf9zKdnULje-OnCuPJa-sb43XPDz"
            "Quq-iQ\"},"
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"use\":\"sig\",\"kid\":\"ps512_keyid\",\"alg\":\"PS512\","
            "\"n\":\"hcPDlRPlk7J_Ch6CcPu93L5TsESmYcnJbSfKhobOqpKmoQGDVB6wE-"
            "Lel5nZzB23-rPpBa0tUPeWHn0g4vJ9vGjZQMDYvJJ3q5QPkO-"
            "KGqigNAMGQkv4tc2y6tdutuZ9q6ftlsysDuooKrtpNDMHOhUIIVf8W9aaMrJa5PwnjoLS0eaZQVKi_"
            "uMYY7gdICjKP173GYJg733Tvv0rVTZsRvFgj5EQRx2e8vfFxJFtXGUcAEpEViYKwWjjvwJDlDsI_"
            "zjiggAgoIQfZJxqRuyYb5FAuIUJyd9fNM67YzidYrS3JzoBftrOWU2trO5lqpt0VE-Y98UzLslnGEXiBrcXrQ"
            "\"},"
        "{\"kty\":\"EC\",\"d\":\"N8JUNzTt-alwdQjCCDzdoGMebq1OaQl7OZpsUbx0x9Y\","
            "\"crv\":\"secp256k1\",\"kid\":\"es256k_keyid\",\"x\":\"IMlyAhf2nqY1gTtVuQhHjr8sJGTR9UA"
            "lqEyIApUNZKw\",\"y\":\"Rv32cdCqWoDhcWE-fep8xMg2SR3hqi4Ter0J9Jk_Vb8\","
            "\"alg\":\"ES256K\"},"
        "{\"kty\":\"EC\",\"d\":\"dKm92BVXljbysYDDd731iTdUjzMfzRpLkZ1SaGbxYIhdgG_YYkWIQXMO0NqJFcTN\""
            ",\"crv\":\"P-384\",\"kid\":\"es384_keyid\",\"x\":\"ptnqg7gKUWKg5-gD2b1KMV5LiufPsimwabF"
            "Ru9ENCMydclBe-_28iPpBzisy8iTz\",\"y\":\"KsnRa6QVJPb8BCOjPGVTX48cOeoT6BrdqYtzK1IiA8L7MI"
            "wngxTVLKptlizEPMDS\",\"alg\":\"ES384\"},"
        "{\"kty\":\"EC\",\"d\":\"AHhv8WAkNgZApSn3o-xJMwbL1SD8XeUUZJrQwefFFc7K2iKVbeFq2ST0P3SwRcBJWS"
            "Cv1cFAuKNWpZ82G5aqxzze\",\"crv\":\"P-521\",\"kid\":\"es512_keyid\",\"x\":\"AXG9qhKzCrm"
            "b0f_8tvC4l1yl2WG60qWCgp_OPe05WR-9ZnfveJMYzyYF8xOveSesMWEKzWxnjbai3kUuwxsD52ep\",\"y\":"
            "\"AZ_lP_rrZx8Z6sxIoaFlbgvHqHgFxWEGicBbXNofgXwuOuHKKsq7KSNSmh2hjTzoPVOAM1sXmMC_nhQgmtKC"
            "ZnLj\",\"alg\":\"ES512\"},"
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"kid\":\"rs256_keyid\",\"alg\":\"RS256\",\"n\":\"3dkN0LTL"
            "vH9wl-vL-MYXtVsvyd4NS9oatGzPfJWTIUOii-N7SmMV383XHfysAm6M_DTqW3HOxzDF0hLIMXzqUDjyQizGIZ"
            "37RkF4GqIcOSEYwkc2IWVnWl4WcSK-2KUlwMe3PpXdxtVZBFGdOVkwbXrdsFiYU11kRhfTbz0pP3lmm84QEzCr"
            "P9Jueu1zqeyj_SBLUszNkgofp_DpTVPKTVtkkqqNYBRF7HhPgR3G2F90NCfHMTjUQICFNP-HT-UO7XS35dmqBJ"
            "NgAO7aIiokrZhl3TrQUrknwlxBTF3gv1Zjru1YG6k_lTHVFcVN3pY-Lr2IiJUdppgpreklY7n8jw\"},"
        "{\"kty\":\"RSA\",\"e\":\"AQAB\",\"kid\":\"ps256_keyid\",\"alg\":\"PS256\",\"n\":\"o2vAJwdv"
            "kyIICvLRvvGn0rEsoSkQHFyPIVoELqKXNxBke8Xkn-yxe1zWx62d71h6ewIpPPHZ7K5lMZJbj-XqJIy-g2_7ZW"
            "ujQhmE_v1JY3TNkAGNsKJupBWPqsyUzEou-ApERvj-IHASQ-zinitDUxhFPb-1q32T1gRgzU2YWVDTa7StgPxu"
            "5cu-GuCeh5uc5FCmCYvtD2TOzWWbh8qiESmYTFR_4n6gQyt_v16iRJCDuR7_HUG0LlgDf78AJDQOZllPXG8Kcj"
            "1_Y1lOosUWg0hjMIS6KqB2nQ-PiLoc9QqOklfHDy2JHrOFb5P1S6vmK75JL3kdd1EyxkgVOWAEiQ\"},"
        "{\"kty\":\"EC\",\"d\":\"zapLxXUHHWO5eI1IOhzYIX0kD9WdWz4jo-tXK6h2xxs\",\"crv\":\"P-256\",\""
            "kid\":\"es256_keyid\",\"x\":\"rOWQ2GjcxRosI46VL16zTL3Vlti3ri0yLU6J6H0Nw-0\",\"y\":\"6o"
            "LwV5zfEmrkmZIBjat92enB3F-wy9-7uoywAdGRc2E\",\"alg\":\"ES256\"}"
    "]}";
const std::string CLAIM_KEY = "sub";
const std::string ISSUER = "anyissuer";
const std::string AUDIENCE = "anyaud";
const std::string SUBJECT = "anysub";
const std::string CUSTOM_KEY = "customkey";

}  // namespace yb::util::testing
