
-- Table `CoMPaNy`
CREATE TABLE "CoMPaNy" (
  id_company SERIAL UNIQUE,
  "IBAN" TEXT,
  NAME TEXT
);

INSERT INTO "CoMPaNy"
VALUES (1991,'12345677890','Cyberdyne Systems');

CREATE TABLE people AS SELECT 'John' AS firstname;
