#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define FUNCTION_PROTOTYPE "SQLITE_PRIVATE int sqlite3ExecuteCompiledSql(Parse *pParse, const char *zSql, char **pzErrMsg)"
#define DUMMY_SQL "SELECT * FROM sqlite_master"

/* Usage: compiler db sql ...*/
int main( int argc, char **argv ){
  if( argc < 2 ){
    fprintf(stderr, "Database path or SQL not provided. Appending empty function.\n");
    FILE *sqlite_source = freopen("sqlite3.c","a",stdout);
    if( sqlite_source==NULL ){
      fprintf(stderr, "Unable to open sqlite3.c\n");
      exit(1);
    }
    printf( FUNCTION_PROTOTYPE " {\n"
            "  return 0;\n"
            "}\n"
    );
  }
  else{
    sqlite3 *db;
    sqlite3_stmt *pStmt=NULL;

    if( sqlite3_open(argv[1], &db)!=SQLITE_OK ){
      fprintf(stderr,"Error: unable to open database \"%s\": %s\n",
        argv[1],sqlite3_errmsg( db ));
      exit(1);
    }

    /* Dummy SQL to force SQLite initialisation */
    freopen("nul","w",stdout); /* ignore if failed */
    sqlite3_prepare_v2(db,DUMMY_SQL,strlen( DUMMY_SQL ),&pStmt,NULL);
    sqlite3_finalize( pStmt );

    FILE *sqlite_source = freopen("sqlite3.c","a",stdout);
    if( sqlite_source==NULL ){
      fprintf(stderr,"Unable to open sqlite3.c\n");
      exit(1);
    }

    printf (FUNCTION_PROTOTYPE " {\n  ");
    for( int i=2; i<argc; ++i ){
      printf( "if( strcmp(zSql, \"%s\")==0 ){\n",argv[i] );
      sqlite3_prepare_v2(db,argv[i],strlen( argv[i] ),&pStmt,NULL);
      if( SQLITE_OK!=sqlite3_errcode(db) ){
        fprintf(stderr,"Error: unable to prepare SQL \"%s\": %s\n",
          argv[i],sqlite3_errmsg( db ));
      }
      sqlite3_finalize( pStmt );
      printf( "\n  }\n  else " );
    }
    printf( "return 0; /* SQL is not compiled */\n"
            "  pParse->zTail=zSql+strlen( zSql );\n"
            "  return 1;\n"
            "}\n"
    );
    sqlite3_close( db );
  }
}
