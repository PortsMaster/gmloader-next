LUA_SRC= \
	./3rdparty/lua/lzio.c \
	./3rdparty/lua/lctype.c \
	./3rdparty/lua/lopcodes.c \
	./3rdparty/lua/lmem.c \
	./3rdparty/lua/lundump.c \
	./3rdparty/lua/ldump.c \
	./3rdparty/lua/lstate.c \
	./3rdparty/lua/lgc.c \
	./3rdparty/lua/llex.c \
	./3rdparty/lua/lcode.c \
	./3rdparty/lua/lparser.c \
	./3rdparty/lua/ldebug.c \
	./3rdparty/lua/lfunc.c \
	./3rdparty/lua/lobject.c \
	./3rdparty/lua/ltm.c \
	./3rdparty/lua/lstring.c \
	./3rdparty/lua/ltable.c \
	./3rdparty/lua/ldo.c \
	./3rdparty/lua/lvm.c \
	./3rdparty/lua/lapi.c \
	./3rdparty/lua/lauxlib.c \
	./3rdparty/lua/lbaselib.c \
	./3rdparty/lua/lcorolib.c \
	./3rdparty/lua/ldblib.c \
	./3rdparty/lua/liolib.c \
	./3rdparty/lua/lmathlib.c \
	./3rdparty/lua/loadlib.c \
	./3rdparty/lua/loslib.c \
	./3rdparty/lua/lstrlib.c \
	./3rdparty/lua/ltablib.c \
	./3rdparty/lua/lutf8lib.c \
	./3rdparty/lua/linit.c


LUA_OBJ=\
	$(patsubst ./3rdparty/lua/%.c,./build/${ARCH}/lua/%.c.o,${LUA_SRC})

# libzip.h and zipconf.h and the flags are auto-generated by CMake
LUA_CONFIG:=-include 3rdparty/lua/lprefix.h -include 3rdparty/lua/luaconf.h -DLUA_USE_LINUX=1 -DLUA_LIB
LUA_FLAGS=$(CFLAGS)

build/${ARCH}/lua/%.c.o: 3rdparty/lua/%.c
	@mkdir -p $(@D)
	$(CC) $(LUA_CONFIG) $(LUA_FLAGS) -c $< -o $@
