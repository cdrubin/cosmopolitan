#if 0
/*─────────────────────────────────────────────────────────────────╗
│ To the extent possible under law, Justine Tunney has waived      │
│ all copyright and related or neighboring rights to this file,    │
│ as it is written in the following disclaimers:                   │
│   • http://unlicense.org/                                        │
│   • http://creativecommons.org/publicdomain/zero/1.0/            │
╚─────────────────────────────────────────────────────────────────*/
#endif
#include "third_party/lua/lauxlib.h"
#include "third_party/lua/lualib.h"


/* Desirable bits of LuaFileSystem added BELOW */

#define LFS_EXPORT
#define LFS_MAXPATHLEN 260
#define LFS_LIBNAME "lfs"
#define LFS_VERSION "1.8.0"

#define STAT_STRUCT struct stat
#define STAT_FUNC stat
#define LSTAT_FUNC lstat

#define lfs_mkdir(path) (mkdir((path), \
    S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH))

#include "libc/calls/struct/dirent.h"

#define DIR_METATABLE "directory metatable"
typedef struct dir_data {
  int closed;
  DIR *dir;
} dir_data;

#include "libc/stdio/stdio.h"
#include "libc/calls/calls.h"
#include "libc/calls/utime.c"
#include "libc/sysv/errfuns.h"
#include "libc/time/struct/utimbuf.h"
#include "libc/calls/weirdtypes.h"



/*
** Utility functions
*/
static int pusherror(lua_State * L, const char *info)
{
  lua_pushnil(L);
  if (info == NULL)
    lua_pushstring(L, strerror(errno));
  else
    lua_pushfstring(L, "%s: %s", info, strerror(errno));
  lua_pushinteger(L, errno);
return 3;
}


static int pushresult(lua_State * L, int res, const char *info)
{
  if (res == -1) {
    return pusherror(L, info);
  } else {
    lua_pushboolean(L, 1);
    return 1;
  }
}



/*
** This function changes the working (current) directory
*/
static int change_dir(lua_State * L)
{
  const char *path = luaL_checkstring(L, 1);
  if (chdir(path)) {
    lua_pushnil(L);
    lua_pushfstring(L, "Unable to change working directory to '%s'\n%s\n",
                    path, strerror);
    return 2;
  } else {
    lua_pushboolean(L, 1);
    return 1;
  }
}


/*
** This function returns the current directory
** If unable to get the current directory, it returns nil
**  and a string describing the error
*/
static int get_dir(lua_State * L)
{
  char *path = NULL;
  /* Passing (NULL, 0) is not guaranteed to work.
     Use a temp buffer and size instead. */
  size_t size = LFS_MAXPATHLEN; /* initial buffer size */
  int result;
  while (1) {
    char *path2 = realloc(path, size);
    if (!path2) {               /* failed to allocate */
      result = pusherror(L, "get_dir realloc() failed");
      break;
    }
    path = path2;
    if (getcwd(path, size) != NULL) {
      /* success, push the path to the Lua stack */
      lua_pushstring(L, path);
      result = 1;
      break;
    }
    if (errno != ERANGE) {      /* unexpected error */
      result = pusherror(L, "get_dir getcwd() failed");
      break;
    }
    /* ERANGE = insufficient buffer capacity, double size and retry */
    size *= 2;
  }
  free(path);
  return result;
}



/*
** Creates a directory.
** @param #1 Directory path.
*/
static int make_dir(lua_State * L)
{
  const char *path = luaL_checkstring(L, 1);
  return pushresult(L, lfs_mkdir(path), NULL);
}


/*
** Removes a directory.
** @param #1 Directory path.
*/
static int remove_dir(lua_State * L)
{
  const char *path = luaL_checkstring(L, 1);
  return pushresult(L, rmdir(path), NULL);
}


/*
** Directory iterator
*/
static int dir_iter(lua_State * L)
{
  struct dirent *entry;
  dir_data *d = (dir_data *) luaL_checkudata(L, 1, DIR_METATABLE);
  luaL_argcheck(L, d->closed == 0, 1, "closed directory");
  
  /* NOTE: directory iteration over the zip: virtual fs is not supported by Cosmo presently */
  if ((entry = readdir(d->dir)) != NULL) {
    lua_pushstring(L, entry->d_name);
    return 1;
  } else {
    /* no more entries => close directory */
    closedir(d->dir);
    d->closed = 1;
    return 0;
  }
}


/*
** Closes directory iterators
*/
static int dir_close(lua_State * L)
{
  dir_data *d = (dir_data *) lua_touserdata(L, 1);
  if (!d->closed && d->dir) {
    closedir(d->dir);
  }
  d->closed = 1;
  return 0;
}


/*
** Factory of directory iterators
*/
static int dir_iter_factory(lua_State * L)
{
  const char *path = luaL_checkstring(L, 1);
  dir_data *d;
  lua_pushcfunction(L, dir_iter);
  d = (dir_data *) lua_newuserdata(L, sizeof(dir_data));
  luaL_getmetatable(L, DIR_METATABLE);
  lua_setmetatable(L, -2);
  d->closed = 0;
  d->dir = opendir(path);
  if (d->dir == NULL)
    luaL_error(L, "cannot open %s: %s", path, strerror(errno));
#if LUA_VERSION_NUM >= 504
  lua_pushnil(L);
  lua_pushvalue(L, -2);
  return 4;
#else
  return 2;
#endif
}


static int file_utime(lua_State * L)
{
  const char *file = luaL_checkstring(L, 1);
  struct utimbuf utb, *buf;

  if (lua_gettop(L) == 1)       /* set to current date/time */
    buf = NULL;
  else {
    utb.actime = (time_t) luaL_optnumber(L, 2, 0);
    utb.modtime = (time_t) luaL_optinteger(L, 3, utb.actime);
    buf = &utb;
  }

  return pushresult(L, utime(file, buf), NULL);
}



static const char *mode2string(mode_t mode)
{
  if (S_ISREG(mode))
    return "file";
  else if (S_ISDIR(mode))
    return "directory";
  else if (S_ISLNK(mode))
    return "link";
  else if (S_ISSOCK(mode))
    return "socket";
  else if (S_ISFIFO(mode))
    return "named pipe";
  else if (S_ISCHR(mode))
    return "char device";
  else if (S_ISBLK(mode))
    return "block device";
  else
    return "other";
}


/* inode protection mode */
static void push_st_mode(lua_State * L, STAT_STRUCT * info)
{
  lua_pushstring(L, mode2string(info->st_mode));
}

/* device inode resides on */
static void push_st_dev(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_dev);
}

/* inode's number */
static void push_st_ino(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_ino);
}

/* number of hard links to the file */
static void push_st_nlink(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_nlink);
}

/* user-id of owner */
static void push_st_uid(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_uid);
}

/* group-id of owner */
static void push_st_gid(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_gid);
}

/* device type, for special file inode */
static void push_st_rdev(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_rdev);
}

/* time of last access */
static void push_st_atime(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_atime);
}

/* time of last data modification */
static void push_st_mtime(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_mtime);
}

/* time of last file status change */
static void push_st_ctime(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_ctime);
}

/* file size, in bytes */
static void push_st_size(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_size);
}

#ifndef _WIN32
/* blocks allocated for file */
static void push_st_blocks(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_blocks);
}

/* optimal file system I/O blocksize */
static void push_st_blksize(lua_State * L, STAT_STRUCT * info)
{
  lua_pushinteger(L, (lua_Integer) info->st_blksize);
}
#endif

 /*
  ** Convert the inode protection mode to a permission list.
  */

#ifdef _WIN32
static const char *perm2string(unsigned short mode)
{
  static char perms[10] = "---------";
  int i;
  for (i = 0; i < 9; i++)
    perms[i] = '-';
  if (mode & _S_IREAD) {
    perms[0] = 'r';
    perms[3] = 'r';
    perms[6] = 'r';
  }
  if (mode & _S_IWRITE) {
    perms[1] = 'w';
    perms[4] = 'w';
    perms[7] = 'w';
  }
  if (mode & _S_IEXEC) {
    perms[2] = 'x';
    perms[5] = 'x';
    perms[8] = 'x';
  }
  return perms;
}
#else
static const char *perm2string(mode_t mode)
{
  static char perms[10] = "---------";
  int i;
  for (i = 0; i < 9; i++)
    perms[i] = '-';
  if (mode & S_IRUSR)
    perms[0] = 'r';
  if (mode & S_IWUSR)
    perms[1] = 'w';
  if (mode & S_IXUSR)
    perms[2] = 'x';
  if (mode & S_IRGRP)
    perms[3] = 'r';
  if (mode & S_IWGRP)
    perms[4] = 'w';
  if (mode & S_IXGRP)
    perms[5] = 'x';
  if (mode & S_IROTH)
    perms[6] = 'r';
  if (mode & S_IWOTH)
    perms[7] = 'w';
  if (mode & S_IXOTH)
    perms[8] = 'x';
  return perms;
}
#endif

/* permssions string */
static void push_st_perm(lua_State * L, STAT_STRUCT * info)
{
  lua_pushstring(L, perm2string(info->st_mode));
}

typedef void (*_push_function)(lua_State * L, STAT_STRUCT * info);

struct _stat_members {
  const char *name;
  _push_function push;
};

struct _stat_members members[] = {
  { "mode", push_st_mode },
  { "dev", push_st_dev },
  { "ino", push_st_ino },
  { "nlink", push_st_nlink },
  { "uid", push_st_uid },
  { "gid", push_st_gid },
  { "rdev", push_st_rdev },
  { "access", push_st_atime },
  { "modification", push_st_mtime },
  { "change", push_st_ctime },
  { "size", push_st_size },
  { "permissions", push_st_perm },
#ifndef _WIN32
  { "blocks", push_st_blocks },
  { "blksize", push_st_blksize },
#endif
  { NULL, NULL }
};

/*
** Get file or symbolic link information
*/
static int _file_info_(lua_State * L,
                       int (*st)(const char *, STAT_STRUCT *))
{
  STAT_STRUCT info;
  const char *file = luaL_checkstring(L, 1);
  int i;

  if (st(file, &info)) {
    lua_pushnil(L);
    lua_pushfstring(L, "cannot obtain information from file '%s': %s",
                    file, strerror(errno));
    lua_pushinteger(L, errno);
    return 3;
  }
  if (lua_isstring(L, 2)) {
    const char *member = lua_tostring(L, 2);
    for (i = 0; members[i].name; i++) {
      if (strcmp(members[i].name, member) == 0) {
        /* push member value and return */
        members[i].push(L, &info);
        return 1;
      }
    }
    /* member not found */
    return luaL_error(L, "invalid attribute name '%s'", member);
  }
  /* creates a table if none is given, removes extra arguments */
  lua_settop(L, 2);
  if (!lua_istable(L, 2)) {
    lua_newtable(L);
  }
  /* stores all members in table on top of the stack */
  for (i = 0; members[i].name; i++) {
    lua_pushstring(L, members[i].name);
    members[i].push(L, &info);
    lua_rawset(L, -3);
  }
  return 1;
}


/*
** Get file information using stat.
*/
static int file_info(lua_State * L)
{
  return _file_info_(L, STAT_FUNC);
}


/*
** Creates directory metatable.
*/
static int dir_create_meta(lua_State * L)
{
  luaL_newmetatable(L, DIR_METATABLE);

  /* Method table */
  lua_newtable(L);
  lua_pushcfunction(L, dir_iter);
  lua_setfield(L, -2, "next");
  lua_pushcfunction(L, dir_close);
  lua_setfield(L, -2, "close");

  /* Metamethods */
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, dir_close);
  lua_setfield(L, -2, "__gc");

  lua_pushcfunction(L, dir_close);
  lua_setfield(L, -2, "__close");
  return 1;
}


/*
** Assumes the table is on top of the stack.
*/
static void set_info(lua_State * L)
{
  lua_pushliteral(L, "Copyright (C) 2003-2017 Kepler Project");
  lua_setfield(L, -2, "_COPYRIGHT");
  lua_pushliteral(L,
                  "LuaFileSystem is a Lua library developed to complement "
                  "the set of functions related to file systems offered by "
                  "the standard Lua distribution");
  lua_setfield(L, -2, "_DESCRIPTION");
  lua_pushliteral(L, "LuaFileSystem " LFS_VERSION);
  lua_setfield(L, -2, "_VERSION");
}


static const struct luaL_Reg fslib[] = {
  { "attributes", file_info },
  { "chdir", change_dir },
  { "currentdir", get_dir },
  { "dir", dir_iter_factory },
  { "mkdir", make_dir },
  { "rmdir", remove_dir },
  { "touch", file_utime },
  { NULL, NULL },
};

LFS_EXPORT int luaopen_lfs(lua_State * L)
{
  dir_create_meta(L);
  luaL_newlib(L, fslib);
  lua_pushvalue(L, -1);
  lua_setglobal(L, LFS_LIBNAME);
  set_info(L);
  return 1;
}


/* Desirable bits of LuaFileSystem added ABOVE */


// example c function to export to lua
int NativeAdd(lua_State *L) {
  lua_Number x, y;
  x = luaL_checknumber(L, 1);
  y = luaL_checknumber(L, 2);
  lua_pushnumber(L, x + y);
  return 1; /* number of results */
}

int main(int argc, char *argv[]) {
  lua_State *L;
  L = luaL_newstate();
  luaL_openlibs(L);
  
  // add lfs
  luaopen_lfs(L);

  // make example C function available
  lua_pushcfunction(L, NativeAdd);  
  lua_setglobal(L, "NativeAdd");
  
  // set lua arg global to command line arguments
  lua_createtable( L, argc, 0 );
  int table_index = lua_gettop( L );
  for ( int i = 0; i < argc; ++i ) {
    lua_pushstring( L, argv[i] );
    lua_rawseti( L, table_index, i );
  }
  lua_setglobal( L, "arg" );
  
    
  if (luaL_dofile(L, "zip:main.lua") != LUA_OK) {
    fprintf( stderr, "%s", lua_tostring(L,-1) );
  }
  
  lua_close(L);
  return 0;
}
