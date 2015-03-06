/*
** 2015 March 6
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains C code that uses the SQL translator
** to generate C functions for the given SQL.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define FUNCTION_START "\n" \
  "SQLITE_PRIVATE int sqlite3ExecuteCompiledSql(Parse *pParse, const char *zSql, char **pzErrMsg){\n"
#define FUNCTION_END "}\n"

#define SQL_FUNCTION_START "\n" \
  "SQLITE_PRIVATE int sqlite3ExecuteCompiledSql%u(Parse *pParse, const char *zSql, char **pzErrMsg){\n"
#define SQL_FUNCTION_END "  return 0;\n}\n"
#define SQL_FUNCTION_CALL "sqlite3ExecuteCompiledSql%u(pParse,zSql,pzErrMsg)"

#define DUMMY_SQL "SELECT * FROM sqlite_master"

#define CREATE_TBL_COMPILED_SQL "\
CREATE TEMPORARY TABLE IF NOT EXISTS [compiled_sql] (\
[hash] INTEGER, \
[sql] TEXT, \
[csql] TEXT, \
UNIQUE([sql]))"
#define INSERT_COMPILED_SQL "\
INSERT OR IGNORE INTO [temp].[compiled_sql] \
([hash], [sql], [csql]) \
VALUES (?,?,?)"
#define READ_COMPILED_SQL "\
SELECT [hash],[sql],[csql] \
FROM [temp].[compiled_sql] \
ORDER BY [hash]"
#define READ_UNIQUE_HASH "\
SELECT DISTINCT([hash]) \
FROM [temp].[compiled_sql]"

extern void (*sqlite3CompiledSql)(const char *zSql, int nSql, const char *zCSql);

static sqlite3 *gCompiledSqlDb;
static sqlite3_stmt *gInsertStmt;

unsigned get_hash(const char *zSql, size_t nChar){
  /* FNV hash generator */
  unsigned hash=0;
  for( size_t i=0;i<nChar;++i ){
    hash^=zSql[i];
    hash+=(hash<<1)+(hash<<4)+(hash<<7)+(hash<<8)+(hash<<24);
  }
  return hash;
}

void escape_sql(const unsigned char *zSql, size_t nChar, char *zEscaped){
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

void prepare_sql(sqlite3 *db, char *zSql){
  sqlite3_stmt *pStmt=NULL;
  size_t sqllen=strlen( zSql );

  sqlite3_prepare_v2(db,zSql,sqllen,&pStmt,NULL);
  if( SQLITE_OK!=sqlite3_errcode(db) ){
    fprintf(stderr,"Error: unable to prepare SQL \"%s\": %s\n",
      zSql,sqlite3_errmsg( db ));
  }
  else{
    sqlite3_step( pStmt );
  }
  sqlite3_finalize( pStmt );
}

void compile_sql(const char *zSql, int nSql, const char *zCSql){
  unsigned hash=get_hash( zSql,nSql );
  sqlite3_bind_int64( gInsertStmt,1,hash );
  sqlite3_bind_text( gInsertStmt,2,zSql,nSql,SQLITE_STATIC );
  sqlite3_bind_text( gInsertStmt,3,zCSql,strlen( zCSql ),SQLITE_STATIC );
  sqlite3_step( gInsertStmt );
  sqlite3_reset( gInsertStmt );
}

void save_code(sqlite3 *db){
  sqlite3_stmt *pStmt=NULL;

  printf( "\n#ifdef ENABLE_COMPILED_SQL\n" );

  /* Each SQL hash gets saved as a function.
   * This prevents stack overflow when compiling a lot of SQL. */
  sqlite3_prepare_v2( db,READ_COMPILED_SQL,sizeof(READ_COMPILED_SQL)+1,&pStmt,NULL );
  sqlite3_int64 previous_hash = -1;
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    sqlite3_int64 hash=sqlite3_column_int64( pStmt,0 );
    const unsigned char *zSql = sqlite3_column_text( pStmt,1 );
    int nSql = sqlite3_column_bytes( pStmt,1 );
    const unsigned char *zCSql = sqlite3_column_text( pStmt,2 );

    char *sqlescaped=malloc( nSql*2 );
    escape_sql( zSql,nSql,sqlescaped );

    if( hash==previous_hash ){
      printf( "  else " );
    }
    else{
      if( previous_hash!=-1 ){
        printf( SQL_FUNCTION_END );
      }
      printf( SQL_FUNCTION_START,(unsigned)hash );
      printf( "  " );
    }
    printf( "if( strcmp( zSql,\"%s\" )==0 ){\n",sqlescaped );
    printf( "%s",zCSql );
    printf( "\n    return 1;\n" );
    printf( "  }\n" );

    previous_hash=hash;
    free( sqlescaped );
  }
  if( previous_hash!=-1 ){
    printf( SQL_FUNCTION_END );
  }
  sqlite3_finalize( pStmt );

  /* Print the main function. */
  sqlite3_prepare_v2( db,READ_UNIQUE_HASH,sizeof(READ_UNIQUE_HASH)+1,&pStmt,NULL );
  printf( FUNCTION_START );
  printf( "  unsigned hash=0;\n" );
  printf( "  const char *zTail=zSql;\n\n" );
  printf( "  /* FNV hash generator */\n" );
  printf( "  while( *zTail ){\n" );
  printf( "    hash^=*(zTail++);\n" );
  printf( "    hash+=(hash<<1)+(hash<<4)+(hash<<7)+(hash<<8)+(hash<<24);\n" );
  printf( "  }\n" );
  printf( "  switch( hash ){\n" );
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    unsigned hash = (unsigned)sqlite3_column_int64( pStmt,0 );
    printf( "    case %u:\n",hash );
    printf( "      if( " SQL_FUNCTION_CALL "==0 ) return 0;\n",hash );
    printf( "      break;\n" );
  }
  printf( "    default: return 0; /* SQL is not compiled */\n"
          "  }\n"
          "  pParse->zTail=zTail;\n"
          "  return 1;\n"
          FUNCTION_END
  );

  printf( "#endif /* ENABLE_COMPILED_SQL */\n" );
}

void emit_code(){
  sqlite3_finalize( gInsertStmt );
  sqlite3CompiledSql = NULL;

  FILE *sqlite_source = freopen("sqlite3.c","a",stdout);
  if( sqlite_source==NULL ) fprintf(stderr,"Unable to open sqlite3.c\n");
  else save_code( gCompiledSqlDb );
  sqlite3_close( gCompiledSqlDb );
}

void initialize_compiler(){
  if( sqlite3_open(":memory:", &gCompiledSqlDb)!=SQLITE_OK ){
    fprintf(stderr,"Error: unable to open in-memory database: %s\n",
      sqlite3_errmsg( gCompiledSqlDb ));
    exit(1);
  }
  prepare_sql( gCompiledSqlDb,CREATE_TBL_COMPILED_SQL );
  sqlite3_prepare_v2( gCompiledSqlDb,INSERT_COMPILED_SQL,
    sizeof( INSERT_COMPILED_SQL )+1,&gInsertStmt,NULL );

  sqlite3CompiledSql = compile_sql;
  if( atexit(emit_code)!=0 ){
    fprintf( stderr,"Unable to register emit_code.\n" );
  }
}

/* Usage: compiler db sql ...
 * or line separated SQL via stdin
 */
int main( int argc,char **argv ){
  if( argc < 2 ){
    fprintf(stderr, "Database path or SQL not provided. Appending empty function.\n");
    FILE *sqlite_source = freopen("sqlite3.c","a",stdout);
    if( sqlite_source==NULL ){
      fprintf(stderr, "Unable to open sqlite3.c\n");
      exit(1);
    }
    printf( FUNCTION_START
            "  return 0;\n"
            FUNCTION_END
    );
  }
  else{
    sqlite3 *db;

    if( sqlite3_open(argv[1], &db)!=SQLITE_OK ){
      fprintf(stderr,"Error: unable to open database \"%s\": %s\n",
        argv[1],sqlite3_errmsg( db ));
      exit(1);
    }

#ifdef DISABLE_INTIALISATION
    /* Dummy SQL to force initialisation of sqlite_master, schema, etc. */
    prepare_sql( db,DUMMY_SQL );
#endif /* DISABLE_INTIALISATION */

    initialize_compiler();

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
          prepare_sql( db,sql );
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
        prepare_sql( db,argv[i] );
      }
    }

    sqlite3_close( db );
  }
}
