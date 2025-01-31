/* Processed by ecpg (regression mode) */
/* These include files are added by the preprocessor */
#include <ecpglib.h>
#include <ecpgerrno.h>
#include <sqlca.h>
/* End of automatic include section */
#define ECPGdebug(X,Y) ECPGdebug((X)+100,(Y))

#line 1 "test1.pgc"
/*
 * this file tests all sorts of connecting to one single database.
 */

#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* do not include regression.h */

int
main(void)
{
/* exec sql begin declare section */
	 
	 

#line 16 "test1.pgc"
 char db [ 200 ] ;
 
#line 17 "test1.pgc"
 char pw [ 200 ] ;
/* exec sql end declare section */
#line 18 "test1.pgc"


	ECPGdebug(1, stderr);

	{ ECPGconnect(__LINE__, 0, "ecpg2_regression" , NULL, NULL , "main", 0); }
#line 22 "test1.pgc"

	{ ECPGdo(__LINE__, 0, 1, NULL, 0, ECPGst_normal, "alter user regress_ecpg_user1 encrypted password 'connectpw'", ECPGt_EOIT, ECPGt_EORT);}
#line 23 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 24 "test1.pgc"
  /* <-- "main" not specified */

	{ ECPGconnect(__LINE__, 0, "ecpg2_regression@localhost" , NULL, NULL , "main", 0); }
#line 26 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "main");}
#line 27 "test1.pgc"


	{ ECPGconnect(__LINE__, 0, "@localhost" , "regress_ecpg_user2" , NULL , "main", 0); }
#line 29 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "main");}
#line 30 "test1.pgc"


	/* exec sql connect to :@TEMP_PORT@ as main user regress_ecpg_user2;
	exec sql disconnect main; */

	{ ECPGconnect(__LINE__, 0, "tcp:postgresql://localhost/ecpg2_regression" , "regress_ecpg_user1" , "connectpw" , NULL, 0); }
#line 35 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 36 "test1.pgc"


	{ ECPGconnect(__LINE__, 0, "tcp:postgresql://localhost/" , "regress_ecpg_user2" , NULL , NULL, 0); }
#line 38 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 39 "test1.pgc"


	strcpy(pw, "connectpw");
	strcpy(db, "tcp:postgresql://localhost/ecpg2_regression");
	{ ECPGconnect(__LINE__, 0, db , "regress_ecpg_user1" , pw , NULL, 0); }
#line 43 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 44 "test1.pgc"


	{ ECPGconnect(__LINE__, 0, "unix:postgresql://localhost/ecpg2_regression" , "regress_ecpg_user1" , "connectpw" , NULL, 0); }
#line 46 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 47 "test1.pgc"


	{ ECPGconnect(__LINE__, 0, "unix:postgresql://localhost/ecpg2_regression?connect_timeout=180" , "regress_ecpg_user1" , NULL , NULL, 0); }
#line 49 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 50 "test1.pgc"


	/* wrong db */
	{ ECPGconnect(__LINE__, 0, "tcp:postgresql://localhost/nonexistent" , "regress_ecpg_user1" , "connectpw" , NULL, 0); }
#line 53 "test1.pgc"

	{ ECPGdisconnect(__LINE__, "CURRENT");}
#line 54 "test1.pgc"


	/* wrong port */
	{ ECPGconnect(__LINE__, 0, "tcp:postgresql://127.0.0.1:20/ecpg2_regression" , "regress_ecpg_user1" , "connectpw" , NULL, 0); }
#line 57 "test1.pgc"

	/* no disconnect necessary */

	/* wrong password */
	{ ECPGconnect(__LINE__, 0, "unix:postgresql://localhost/ecpg2_regression" , "regress_ecpg_user1" , "wrongpw" , NULL, 0); }
#line 61 "test1.pgc"

	/* no disconnect necessary */

	return 0;
}
