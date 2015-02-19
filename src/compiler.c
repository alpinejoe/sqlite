#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define FUNCTION_PROTOTYPE "SQLITE_PRIVATE int sqlite3ExecuteCompiledSql(Parse *pParse, const char *zSql, char **pzErrMsg)"
#define DUMMY_SQL "SELECT * FROM sqlite_master"

unsigned get_hash(char *zSql, size_t nChar){
  unsigned hash=0;
  for( size_t i=0;i<nChar;++i ){
    hash^=zSql[i];
    hash+=(hash<<1)+(hash<<4)+(hash<<7)+(hash<<8)+(hash<<24);
  }
  return hash;
}

void escape_sql(char *zSql, size_t nChar, char *zEscaped){
  size_t j=0;
  for( size_t i=0;i<nChar;++i ){
    switch( zSql[i] ){
      case '"':
      case '\\':
        zEscaped[j++]='\\';
        zEscaped[j++]=zSql[i];
        break;
      case '\n':
        zEscaped[j++]='\\';
        zEscaped[j++]='n';
        break;
      case '\t':
        zEscaped[j++]='\\';
        zEscaped[j++]='t';
        break;
      default:
        zEscaped[j++]=zSql[i];
    }
  }
  zEscaped[j]='\0';
}

void compile_sql(sqlite3 *db,char *zSql){
  sqlite3_stmt *pStmt=NULL;
  size_t sqllen=strlen( zSql );
  unsigned hash=get_hash( zSql,sqllen );

  char *sqlescaped=malloc( sqllen*2 );
  escape_sql( zSql,sqllen,sqlescaped );

  printf( "    case %u: ",hash );
  printf( "if( strcmp( zSql,\"%s\" )==0 ){\n",sqlescaped );
  sqlite3_prepare_v2(db,zSql,sqllen,&pStmt,NULL);
  if( SQLITE_OK!=sqlite3_errcode(db) ){
    fprintf(stderr,"Error: unable to prepare SQL \"%s\": %s\n",
      zSql,sqlite3_errmsg( db ));
  }
  else{
    sqlite3_step( pStmt );
  }
  sqlite3_finalize( pStmt );
  free( sqlescaped );
  printf( "\n    }\n" );
  printf( "    break;\n" );
}

/* Usage: compiler db sql ...*/
int main( int argc,char **argv ){
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
    freopen("NUL","w",stdout); /* ignore if failed */
    sqlite3_prepare_v2(db,DUMMY_SQL,strlen( DUMMY_SQL ),&pStmt,NULL);
    sqlite3_finalize( pStmt );

    FILE *sqlite_source = freopen("sqlite3.c","a",stdout);
    if( sqlite_source==NULL ){
      fprintf(stderr,"Unable to open sqlite3.c\n");
      exit(1);
    }

    printf( FUNCTION_PROTOTYPE " {\n" );
    printf( "  unsigned hash=0;\n" );
    printf( "  size_t sqllen=strlen( zSql );\n" );
    printf( "  for( size_t i=0; i<sqllen; ++i ){\n" );
    printf( "    hash^=zSql[i];\n" );
    printf( "    hash+=(hash<<1)+(hash<<4)+(hash<<7)+(hash<<8)+(hash<<24);\n" );
    printf( "  }\n" );
    printf( "  switch( hash ){\n" );
    if( argc==2 ){
      /* Read SQL from stdin. Queries are separated by an empty line. */
      char buffer[1024];
      char *sql=NULL;
      int read;
      int total=0;
      while( fgets( buffer,sizeof( buffer ),stdin ) ){
        read=strlen( buffer );
        if( read==1 && total>0 ){
          sql[total-1]='\0'; /* replace last '\n' */
          compile_sql( db,sql );
          total=0;
        }
        else{
          sql=realloc( sql,total+read+1 );
          memmove( sql+total,buffer,read+1 );
          total+=read;
        }
      }
      free( sql );
    }
    else{
      for( int i=2; i<argc; ++i ){
        compile_sql( db,argv[i] );
      }
    }
    printf( "    default: return 0; /* SQL is not compiled */\n"
            "  }\n"
            "  pParse->zTail=zSql+strlen( zSql );\n"
            "  return 1;\n"
            "}\n"
    );
    sqlite3_close( db );
  }
}
