# Cosmopolitan Lua

## [APE](https://justine.lol/cosmopolitan/index.html) Lua binary using the [Lua C API](https://www.lua.org/pil/24.html) extensions and a _require_ that works inside the zip


To make an APE Lua binary really useful across platforms the following are pre-requisites:

1. Being able to extend the lua base with some platform-specific functions (like parts of [luafilesystem](https://github.com/keplerproject/luafilesystem))
2. Pass along command-line arguments to an initial Lua file
3. Enable Lua sripts to _require_ other Lua files from within the zip archive

---
### 1. Extending Lua with the C API

The [hellolua.c](https://github.com/jart/cosmopolitan/blob/master/examples/hellolua.lua) example gets us much of the way. It includes an example of 
a C ```NativeAdd``` that is added to the global scope for Lua state. Adding desired functions from luafilesystem is a similar process of adding the C sources and
standardizing the ```#include```s to use the Cosmo files. Only the POSIX implementations are required - the magic of [Cosmopolitan libc](https://justine.lol/cosmopolitan/documentation.html) 
is that functions like [readdir](https://justine.lol/cosmopolitan/documentation.html#readdir) can just be used and assumed to work cross-platform. Crumbs. Just don't expect to create a directory iterator over the zip virtual filesystem quite yet.

The [morelua.c](https://github.com/cdrubin/cosmopolitan/blob/master/examples/morelua.lua) example adds some of luafilesystem and registers the ```lfs```
package for use by loaded scripts. It is also likely going to be helpful to make errors visible on __stderr__:

```C
if ( luaL_dofile( L, "zip:main.lua" ) != LUA_OK ) {
  fprintf( stderr, "%s", lua_tostring( L, -1 ) );
}
```

Packages registered using the [luaopen_](https://github.com/cdrubin/cosmopolitan/blob/5a2fead17beb36deeeb9b12afe7d0965fd0ee2be/examples/morelua.c#L627) pattern - canonically used in ```linit.c``` - are added to global scope and do not need to be required. Using something like:

```lua
lfs = lfs or require( 'lfs' )
```
in your Lua scripts will allow them to work within this kind of extended Lua environment and outside of it.


---
### 2. Pass along command-line arguments to an initial Lua file

Using the APE binary will be transparent to most users so they will expect things like command-line parameters to be accessible by the running scripts - just
as they would if Lua was being invoked from a shell script. So be sure to populate and add ```arg``` to the Lua environment

```C
int main( int argc, char *argv[] ) {
  lua_State *L;
  L = luaL_newstate();
  luaL_openlibs( L );
  
  ...
  
  // set lua arg global to command line arguments
  lua_createtable( L, argc, 0 );
  int table_index = lua_gettop( L );
  for ( int i = 0; i < argc; ++i ) {
    lua_pushstring( L, argv[i] );
    lua_rawseti( L, table_index, i );
  }
  lua_setglobal( L, "arg" );

```

---
### 3. Enable Lua sripts to _require_ other Lua files from within the zip archive

Again [hellolua.c](https://github.com/jart/cosmopolitan/blob/master/examples/hellolua.lua) shows us what is required to load a file from the zip archive 
from C:

```c
luaL_dofile( L, "zip:examples/hellolua.lua" );
```
what this doesn't cover though is how any Lua scripts you place in the zip archive will be able to require other scripts in that archive. The solution
is a simple one: alter the ```package.path``` value so that patterns are matched with the ```zip:``` syntax. Cosmopolitan's zip filesystem handling finesses the rest.
Use the following, in your zipped Lua scripts, before any ```require``` statements:

```lua
package.path = 'zip:?.lua'
```
