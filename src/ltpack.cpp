#include "ltpack.h"
#include "common.h"
#define __STDC_LIMIT_MACROS
#ifndef _WIN32
# include <unistd.h>
#endif

#define MAX_DEPTH 128

#define TYPE_INT8   0
#define TYPE_INT16  1
#define TYPE_INT32  2
#define TYPE_INT64  3
#define TYPE_DOUBLE 4
#define TYPE_STRING 5
#define TYPE_BOOL   6
#define TYPE_TABLE_BEG  7
#define TYPE_TABLE_END	8

static inline void
pack_integer(tpack_t *pack, int64_t v) {
	if (v >= INT8_MIN && v <= INT8_MAX) {
		pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_INT8);
		pack->m_data = sdscat(pack->m_data, (int8_t)v);
		return;
	}
	if (v >= INT16_MIN && v <= INT16_MAX) {
		pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_INT16);
		pack->m_data = sdscat(pack->m_data, (int16_t)v);
		return;
	}
	if (v >= INT32_MIN && v <= INT32_MAX) {
		pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_INT32);
		pack->m_data = sdscat(pack->m_data, (int32_t)v);
		return;
	}
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_INT64);
	pack->m_data = sdscat(pack->m_data, (int64_t)v);
}

static inline void
pack_double(tpack_t *pack, double v) {
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_DOUBLE);
	pack->m_data = sdscat(pack->m_data, v);
}

static inline void
pack_boolean(tpack_t *pack, bool v) {
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_BOOL);
	pack->m_data = sdscat(pack->m_data, v);
}

static inline void
pack_string(tpack_t *pack, const char* v, size_t len) {
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_STRING);
	pack->m_data = sdscat(pack->m_data, len);
	pack->m_data = sdscpylen(pack->m_data, v, len);
}

static void
pack_lua_data(tpack_t *pack, lua_State *L, int idx, int depth);

static void
pack_table(tpack_t *pack, lua_State *L, int idx, int depth) {
	if (depth > MAX_DEPTH) {
		luaL_error(L, "Too depth while encoding bson");
	}
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_TABLE_BEG);
	luaL_checkstack(L, 2, NULL);
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		pack_lua_data(pack, L, -2, depth);
		pack_lua_data(pack, L, -1, depth);
		lua_pop(L, 1);
	}
	pack->m_data = sdscat(pack->m_data, (int8_t)TYPE_TABLE_END);
}

static void
pack_lua_data(tpack_t *pack, lua_State *L, int idx, int depth) {
	int data_type = lua_type(L, idx);
	const char * str = NULL;
	size_t len;
	switch (data_type) {
	case LUA_TNUMBER:
		if (lua_isinteger(L, idx)) {
			pack_integer(pack, lua_tointeger(L, idx));
		} else {
			pack_double(pack, lua_tonumber(L, idx));
		}
		return;
	case LUA_TSTRING:
		str = lua_tolstring(L, idx, &len);
		pack_string(pack, str, len);
		return;
	case LUA_TBOOLEAN:
		pack_boolean(pack, (bool)lua_toboolean(L, idx));
		return;
	case LUA_TTABLE:
		pack_table(pack, L, idx, depth + 1);
		return;
	default:
		luaL_error(L, "Invalid data type : %s", lua_typename(L, data_type));
		return;
	}
}

static int8_t
unpack_lua_data(tpack_t *pack, size_t& rpos, lua_State *L) {
	luaL_checkstack(L, 1, NULL);
	int8_t data_type = *(int8_t*)(pack->m_data + rpos);
	size_t len;
	++rpos;
	switch (data_type) {
	case TYPE_INT8:
		lua_pushinteger(L, *(int8_t*)(pack->m_data + rpos));
		rpos += 1;
		break;
	case TYPE_INT16:
		lua_pushinteger(L, *(int16_t*)(pack->m_data + rpos));
		rpos += 2;
		break;
	case TYPE_INT32:
		lua_pushinteger(L, *(int32_t*)(pack->m_data + rpos));
		rpos += 4;
		break;
	case TYPE_INT64:
		lua_pushinteger(L, *(int64_t*)(pack->m_data + rpos));
		rpos += 8;
		break;
	case TYPE_DOUBLE:
		lua_pushnumber(L, *(double*)(pack->m_data + rpos));
		rpos += sizeof(double);
		break;
	case TYPE_STRING:
		len = *(size_t*)(pack->m_data + rpos);
		rpos += sizeof(size_t);
		lua_pushlstring(L, pack->m_data + rpos, len);
		rpos += len;
		break;
	case TYPE_BOOL:
		lua_pushboolean(L, *(bool*)(pack->m_data + rpos));
		rpos += sizeof(bool);
		break;
	case TYPE_TABLE_BEG:
		lua_createtable(L, 0, 0);
		while (unpack_lua_data(pack, rpos, L) != TYPE_TABLE_END) {
			unpack_lua_data(pack, rpos, L);
			lua_rawset(L, -3);
		}
		break;
	case TYPE_TABLE_END:
		break;
	default:
		break;
	}
	return data_type;
}
