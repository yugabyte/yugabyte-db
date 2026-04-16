This package contains Mappers to convert v1 to v2 request/response structures and vice versa.
Here are some guidelines if you need to write a new Mapper.
1. Developer reference https://mapstruct.org/documentation/stable/reference/html/
2. Name the Mapper class as <SourceType>Mapper.java and add a to<TargetType>() method. For example,
   UniverseResponseMapper.java will contain methods to convert from UniverseResp structure, and the
   method toV2UniverseResp() in that Mapper will convert given v1 UniverseResp to v2 UniverseResp.
3. Always inspect the generated Mapper class under target/scala-2.13/classes/api/v2/mappers to
   verify it has the right behaviour. Adjust @Mapping if required.