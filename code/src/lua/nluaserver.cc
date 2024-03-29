#define N_IMPLEMENTS nLuaServer
//--------------------------------------------------------------------
//  nluaserver.cc
//
//  Derived from npythonserver.cc by Jason Asbahr
//  Derived from ntclserver.cc by Andre Weissflog
//  (c) 2003  Matthew T. Welker & Vadim Macagon
//
//  nLuaServer is licensed under the terms of the Nebula License
//--------------------------------------------------------------------

#ifndef N_FILESERVER2_H
#include "kernel/nfileserver2.h"
#endif

#ifndef N_FILE_H
#include "kernel/nfile.h"
#endif

#include "lua/nluaserver.h"
nNebulaClass(nLuaServer, "nscriptserver");

// Initialize static members to NULL
nLuaServer *nLuaServer::Instance = NULL;

extern "C" {
	//Reference for ThunkNebObject
	extern void n_getcmds(void*, nCmd*);
} // extern "C"

// External declaration of Nebula commands
extern int luacmd_Error(lua_State*);
extern int luacmd_Panic(lua_State*);
extern int luacmd_New(lua_State*);
extern int luacmd_NewThunk(lua_State*);
extern int luacmd_Delete(lua_State*);
extern int luacmd_PinThunk(lua_State*);
extern int luacmd_UnpinThunk(lua_State*);
extern int luacmd_Sel(lua_State*);
extern int luacmd_Psel(lua_State*);
extern int luacmd_Get(lua_State*);
extern int luacmd_Exit(lua_State*);
extern int luacmd_Puts(lua_State*);
extern int luacmd_Dir(lua_State*);
extern int luacmd_PushCwd(lua_State*);
extern int luacmd_PopCwd(lua_State*);
extern int luacmd_CmdDispatch(lua_State*);
extern int luacmd_Call(lua_State*);
extern int luacmd_ConCall(lua_State*);
extern int luacmd_Lookup(lua_State*);
extern int luacmd_Mangle(lua_State*);
extern int luacmd_Exists(lua_State*);
extern int luacmd_BeginCmds(lua_State*);
extern int luacmd_AddCmd(lua_State*);
extern int luacmd_EndCmds(lua_State*);
extern int luacmd_IsZombieThunk(lua_State*);
extern int luacmd_DeleteNRef(lua_State*);

void nLuaServer::reg_globalfunc(lua_CFunction func, const char* name)
{
	lua_pushstring(this->L, name);
	lua_pushlightuserdata(L, this);
	lua_pushcclosure(L, func, 1);
	lua_settable(L, LUA_GLOBALSINDEX);
	lua_settop(L, 0);
}

//--------------------------------------------------------------------
//  nLuaServer()
//  Initialize Lua5 interpreter
//--------------------------------------------------------------------
nLuaServer::nLuaServer() :
refFileServer(kernelServer, this),
echo(true),
selgrab(false)
{
	this->refFileServer = "/sys/servers/file2";
	this->indent_level = 0;
	this->indent_buf[0] = 0;

	this->output.clear();

	// Store a handy reference to this instance
	if ( !nLuaServer::Instance )
		nLuaServer::Instance = this;

	// Get a default Lua interpreter up and running then
	this->L = lua_open();

	if ( 0 == this->L ) 
		n_error("Could not create the Lua 5 interpreter");

	// Set up a panic handler
	lua_atpanic(this->L, luacmd_Panic);

	// Load some standard libraries
	luaL_reg lualibs[] = {
		{"baselib", lua_baselibopen},
		{"tablib", lua_tablibopen},  
		{"iolib", lua_iolibopen},
		{"strlib", lua_strlibopen},
		{"mathlib", lua_mathlibopen},
		{NULL, NULL}
	};
	luaL_reg* lreg = lualibs;
	for ( ; lreg->name; lreg++ ) 
	{
		lreg->func(this->L);
		lua_settop(this->L, 0);  /* discard any results */
	}

	// Get a global table of Neb funcs up
	reg_globalfunc(luacmd_Error,            "_ALERT");
	reg_globalfunc(luacmd_New,              "new");
	reg_globalfunc(luacmd_NewThunk,         "newthunk");
	reg_globalfunc(luacmd_Delete,           "delete");
	reg_globalfunc(luacmd_PinThunk,         "pin");
	reg_globalfunc(luacmd_UnpinThunk,       "unpin");
	reg_globalfunc(luacmd_Sel,              "sel");
	reg_globalfunc(luacmd_Psel,             "psel");
	reg_globalfunc(luacmd_Get,              "get");
	reg_globalfunc(luacmd_Exit,             "exit");
	reg_globalfunc(luacmd_Puts,             "puts");
	reg_globalfunc(luacmd_Dir,              "ls");
	reg_globalfunc(luacmd_PushCwd,          "pushcwd");
	reg_globalfunc(luacmd_PopCwd,           "popcwd");
	reg_globalfunc(luacmd_Call,             "call");
	reg_globalfunc(luacmd_ConCall,          "concall");
	reg_globalfunc(luacmd_Lookup,           "lookup");
	reg_globalfunc(luacmd_Mangle,           "mangle");
	reg_globalfunc(luacmd_Exists,           "exists");
	reg_globalfunc(luacmd_BeginCmds,        "begincmds");
	reg_globalfunc(luacmd_AddCmd,           "addcmd");
	reg_globalfunc(luacmd_EndCmds,          "endcmds");
	reg_globalfunc(luacmd_IsZombieThunk,    "IsZombieThunk");
	reg_globalfunc(luacmd_DeleteNRef,       "_delnref");

	// setup environment

	// create the class cache
	this->classCacheName = "nebclasses";
	lua_pushstring(this->L, this->classCacheName.c_str());
	lua_newtable(this->L);
	lua_settable(this->L, LUA_GLOBALSINDEX);

	// create the thunk store (pinned thunks are stored here)
	this->thunkStoreName = "_nebthunks";
	lua_pushstring(this->L, this->thunkStoreName.c_str());
	lua_newtable(this->L);
	lua_settable(this->L, LUA_GLOBALSINDEX);

	// create the nRef metatable
	luaL_dostring(this->L, "_nrefmetatable = { __gc = function(nref) _delnref(nref) end }");

	// load the thunk metatable
	stl_string path;
	this->kernelServer->GetFileServer2()->ManglePath( "home:bin/nebthunker.lua", path);
	luaL_dofile(this->L, path.c_str());

	// clear stack
	lua_settop(this->L, 0);
}

//--------------------------------------------------------------------
//  ~nLuaServer()
//--------------------------------------------------------------------
nLuaServer::~nLuaServer()
{
	lua_close(this->L);
	this->L = 0;
	this->output.clear();
}


//------------------------------------------------------------------------------
//  BeginWrite()
//------------------------------------------------------------------------------
nFile* 
nLuaServer::BeginWrite(const char* filename, nRoot* obj)
{
	n_assert(filename);
	n_assert(obj);

	this->indent_level = 0;

	nFile* file = this->refFileServer->NewFileObject();
	n_assert(file);
	if (file->Open(filename, "w"))
	{
		char buf[N_MAXPATH];
		sprintf(buf, "-- $parser:nluaserver$ $class:%s$\n", obj->GetClass()->GetName());

		file->PutS("-- -----------------------------------------------------------\n");
		file->PutS(buf);
		file->PutS("-- -----------------------------------------------------------\n");

		return file;
	}
	else
	{
		n_printf("nLuaServer::WriteBegin(): failed to open file '%s' for writing!\n", filename);
		delete file;
		return 0;
	}
}

//--------------------------------------------------------------------
//  EndWrite()
//--------------------------------------------------------------------
bool 
nLuaServer::EndWrite(nFile* file)
{
	n_assert(file);

	file->PutS("-- -----------------------------------------------------------\n");
	file->PutS("-- Eof\n");

	file->Close();
	delete file;
	return (this->indent_level == 0);
}

//--------------------------------------------------------------------
//  _indent()
//--------------------------------------------------------------------
static void _indent(long i, char *buf)
{
	long j;
	buf[0] = 0;
	for (j=0; j<i; j++) strcat(buf,"  ");
}

//--------------------------------------------------------------------
//  WriteComment()
//--------------------------------------------------------------------
bool nLuaServer::WriteComment(nFile *file, const char *str)
{
	n_assert(file);
	n_assert(str);
	file->PutS("-- ");
	file->PutS(str);
	file->PutS("\n");
	return true;
}

//--------------------------------------------------------------------
//  write_select_statement()
//  Write the statement to select an object after its creation
//  statement.
//--------------------------------------------------------------------
void nLuaServer::write_select_statement(nFile *file, nRoot *o, nRoot *owner)
{
	// get relative path from owner to o and write select statement
	char relpath[N_MAXPATH];
	_indent(++this->indent_level, this->indent_buf);
	owner->GetRelPath(o, relpath, sizeof(relpath));

	file->PutS(this->indent_buf);
	file->PutS("sel('");
	file->PutS(relpath);
	file->PutS("')\n");
}

//--------------------------------------------------------------------
//  WriteBeginNewObject()
//  Write start of persistent object with default constructor.
//--------------------------------------------------------------------
bool nLuaServer::WriteBeginNewObject(nFile *file, nRoot *o, nRoot *owner)
{
	n_assert(file);
	n_assert(o);
	const char *o_name  = o->GetName();

	// write generic 'new' statement
	const char *o_class = o->GetClass()->GetName();
	_indent(this->indent_level,this->indent_buf);

	file->PutS(this->indent_buf);
	file->PutS("new('");
	file->PutS(o_class);
	file->PutS("','");
	file->PutS(o_name);
	file->PutS("')\n");


	// write select object statement
	this->write_select_statement(file,o,owner);
	return true;
}

//--------------------------------------------------------------------
//  WriteBeginNewObjectCmd()
//  Write start of persistent object with custom constructor
//  defined by command.
//--------------------------------------------------------------------
bool nLuaServer::WriteBeginNewObjectCmd(nFile *file, nRoot *o, nRoot *owner, nCmd *cmd)
{
	n_assert(file);
	n_assert(o);
	n_assert(cmd);

	// write constructor statement
	this->WriteCmd(file, cmd);

	// write select object statement
	this->write_select_statement(file, o, owner);
	return true;
}

//--------------------------------------------------------------------
//  WriteBeginSelObject()
//  Write start of persisting object without constructor, only
//  write the select statement.
//--------------------------------------------------------------------
bool nLuaServer::WriteBeginSelObject(nFile *file, nRoot *o, nRoot *owner)
{
	n_assert(file);
	n_assert(o);

	// write select object statement
	this->write_select_statement(file, o, owner);
	return true;
}

//--------------------------------------------------------------------
//  WriteEndObject()
//--------------------------------------------------------------------
bool nLuaServer::WriteEndObject(nFile *file, nRoot *o, nRoot *owner)
{
	n_assert(file);
	n_assert(o);

	// get relative path from owner to o and write select statement
	char relpath[N_MAXPATH];
	_indent(--this->indent_level, this->indent_buf);
	o->GetRelPath(owner, relpath, sizeof(relpath));

	file->PutS(this->indent_buf);
	file->PutS("sel('");
	file->PutS(relpath);
	file->PutS("')\n");

	return true;
}

//--------------------------------------------------------------------
//  WriteCmd()
//--------------------------------------------------------------------
bool nLuaServer::WriteCmd(nFile *file, nCmd *cmd)
{
	n_assert(file);
	n_assert(cmd);

	const char *name = cmd->GetProto()->GetName();
	n_assert(name);

	//Write the command line
	char buf[N_MAXPATH];
	file->PutS(this->indent_buf);
	file->PutS("call('");
	file->PutS(name);
	file->PutS("'");

	//write the command arguments
	int num_args = cmd->GetNumInArgs();
	cmd->Rewind();

	nArg *arg;
	int i;
	for (i=0; i<num_args; i++) {

		file->PutS(", ");

		arg=cmd->In();

		switch(arg->GetType()) {

			case nArg::ARGTYPE_INT:
				sprintf(buf,"%d",arg->GetI());
				break;

			case nArg::ARGTYPE_FLOAT:
				sprintf(buf,"%.6f",arg->GetF());
				break;

			case nArg::ARGTYPE_STRING:
				file->PutS("[[");
				file->PutS(arg->GetS());
				sprintf(buf,"]]");
				break;

			case nArg::ARGTYPE_BOOL:
				sprintf(buf,"%s",(arg->GetB() ? "true" : "false"));
				break;

			case nArg::ARGTYPE_CODE:
				file->PutS("[[");
				file->PutS(arg->GetS());
				sprintf(buf,"]]");
				break;

			case nArg::ARGTYPE_OBJECT:
				{
					nRoot *o = (nRoot *) arg->GetO();
					if (o) {						
						sprintf(buf, "'%s'", o->GetFullName().c_str());
					} else {
						sprintf(buf, " null");
					}
				}
				break;

			default:
				sprintf(buf," ???");
				break;
		}
		file->PutS(buf);
	}
	file->PutS(")");
	return file->PutS("\n");
}

//--------------------------------------------------------------------
/**
Simple accessor to allow users to get at the Lua context
if needed. Care is needed here not to close the context.
*/
lua_State* nLuaServer::GetContext()
{
	return this->L;
}

//--------------------------------------------------------------------
/**
Add a class to the class cache.

@warning The global class cache table is expected to be on top of
the stack when this method is called.
*/
void nLuaServer::AddClassToCache(lua_State* L, nClass* clazz)
{
	nHashList* protoList = 0;
	nCmdProto* cmdProto;

	// Create the table that will store cmd protos for this class
	// each element will have a key that corresponds to the cmd
	// name and a value which corresponds to a luacmd_CmdDispatch
	// closure that contains the nCmdProto*. In short when you
	// call that value the command is going to be called.
	lua_pushstring(L, clazz->GetName());
	lua_newtable(L);

	do
	{
		protoList = clazz->GetCmdList();
		cmdProto = (nCmdProto*)protoList->GetHead();
		for (; cmdProto; cmdProto = (nCmdProto*)cmdProto->GetSucc()) 
		{
			lua_pushstring(L, cmdProto->GetName());
			lua_pushlightuserdata(L, cmdProto);
			lua_pushcclosure(L, luacmd_CmdDispatch, 1);
			lua_settable(L, -3);
		}
	} while ((clazz = clazz->GetSuperClass()));

	// store table in the global class cache
	lua_settable(L, -3);
}

//--------------------------------------------------------------------
/**
Builds a Lua table for a more natural syntax for getting 
at the NOH and calling cmds.
*/
bool nLuaServer::ThunkNebObject(lua_State* L, nRoot* root)
{
	n_assert(root);

	// create the thunk table
	lua_newtable(L); // 1
	lua_pushstring(L, "_"); // 2
	//lua_pushlightuserdata(L, root); // 3
	// create and push heavy user data on stack
	nRef<nRoot>* ref_Root = n_new nRef<nRoot>(NULL);
	ref_Root->set(root);
	lua_boxpointer(L, ref_Root); // 3
	// give the nRef userdata the proper metatable
	lua_pushstring(L, "_nrefmetatable"); // 4
	lua_gettable(L, LUA_GLOBALSINDEX); // 4
	lua_setmetatable(L, -2); // 4
	lua_settable(L, -3); // add ('_', nRef userdata) to 1
	lua_pushstring(L, "_class"); // 2
	// find the corresponding class table
	lua_pushstring(L, nLuaServer::Instance->classCacheName.c_str()); // 3
	lua_gettable(L, LUA_GLOBALSINDEX); // 3 (nebula classes table)
	n_assert(lua_istable(L, -1) && "nebula classes table not found!");
	const char* className = root->GetClass()->GetName();
	lua_pushstring(L, className); // 4
	lua_gettable(L, -2); // 4 (class table)
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1); // pop the nil off the stack
		// Class table not created yet, do it now.
		nLuaServer::AddClassToCache(L, root->GetClass());
		lua_pushstring(L, className);
		lua_gettable(L, -2);
	}
	lua_remove(L, -2); // remove 3 (nebula classes table)
	lua_settable(L, -3); // add ('_class', class table ref) to thunk
	// give the thunk the proper metatable
	lua_pushstring(L, "_nebthunker"); // 2
	lua_gettable(L, LUA_GLOBALSINDEX); // 2
	lua_setmetatable(L, -2);
	// leave the thunk on the stack and return
	return true;
}

//--------------------------------------------------------------------
/**
Returns a thunk (in the _nebthunks table) that is associated
with the specified key. Or nil a thunk for the specified key
doesn't exist yet.
*/
void nLuaServer::FindThunk(lua_State* L, void* key)
{
	lua_pushstring(L, nLuaServer::Instance->thunkStoreName.c_str());
	lua_rawget(L, LUA_GLOBALSINDEX); // table
	lua_pushlightuserdata(L, key); // key
	lua_rawget(L, -2);
	lua_remove(L, -2); // remove table
}

//--------------------------------------------------------------------
/**
Adds a thunk (assumed to be at the top of the lua stack) to
the _nebthunks table. The thunk will remain on the stack.

@param key The key with which the thunk should be associated.
*/
void nLuaServer::AddThunk(lua_State* L, void* key)
{
	lua_pushstring(L, nLuaServer::Instance->thunkStoreName.c_str());
	lua_rawget(L, LUA_GLOBALSINDEX); // table
	lua_pushlightuserdata(L, key); // key
	lua_pushvalue(L, -3); // value
	lua_rawset(L, -3);
	lua_remove(L, -1); // remove table
}

//--------------------------------------------------------------------
/**
Removes a thunk from the _nebthunks table.

@param key The key that is associated with the thunk.
*/
void nLuaServer::RemoveThunk(lua_State* L, void* key)
{
	lua_pushstring(L, nLuaServer::Instance->thunkStoreName.c_str());
	lua_rawget(L, LUA_GLOBALSINDEX);
	lua_pushlightuserdata(L, key);
	lua_pushnil(L);
	lua_rawset(L, -3);
	lua_remove(L, -1); // remove table
}

//--------------------------------------------------------------------
/**
@brief Simply unwraps a thunk for a passable nRoot* value.
*/
nRoot* nLuaServer::UnpackThunkRoot( lua_State* L, int tableidx )
{
	// push the key on and see what we get back
	// make sure this doesn't chump with the metatables
	nRoot* root;

	lua_pushliteral(L, "_");
	lua_rawget(L, tableidx);
	if (lua_isuserdata(L, -1))
	{
		nRef<nRoot>* ref = (nRef<nRoot>*)lua_unboxpointer(L, -1);
		if ( ref )
			root = ref->get();
		else
			root = 0;
	}
	else
	{
		root = 0;
	}
	lua_pop(L, 1);
	return root;
}

//--------------------------------------------------------------------
/**
@brief Pull a cmd's in-args from the stack and pack them into 
the cmd.
*/
//--------------------------------------------------------------------
bool nLuaServer::StackToInArgs(lua_State* L, nCmd* cmd)
{
	cmd->Rewind();
	nArg* narg;
	int numArgs = cmd->GetNumInArgs();
	// loop through and pack it all in
	for (int i = -numArgs; i < 0; i++)
	{
		// get the arg prepped
		narg = cmd->In();
		if (!nLuaServer::StackToArg(L, narg, i))
			return false;
	}
	return true;
}

//--------------------------------------------------------------------
/**
@brief Create a new table on the stack and populate it with the
members of the list nArg.
*/
void nLuaServer::ListArgToTable(lua_State* L, nArg* narg, bool print)
{
	nArg* listArg;
	int listLen = narg->GetL(listArg);

	lua_newtable(L); // create a table

	if (print)
		n_printf("{");

	for (int j = 0; j < listLen; j++)
	{
		// farq - these are built as a lua table
		lua_pushnumber(L, j);
		switch (listArg->GetType())
		{
		case nArg::ARGTYPE_INT:
			{
				lua_pushnumber(L, listArg->GetI());
				if (print)
				{
					if (j > 0)
						n_printf(", %d", listArg->GetI());
					else
						n_printf("%d", listArg->GetI());
				}
				break;
			}
		case nArg::ARGTYPE_FLOAT:
			{
				lua_pushnumber(L, listArg->GetF());
				if (print)
				{
					if (j > 0)
						n_printf(", %f", listArg->GetF());
					else
						n_printf("%f", listArg->GetF());
				}
				break;
			}
		case nArg::ARGTYPE_STRING:
			{
				lua_pushstring(L, listArg->GetS());
				if (print)
				{
					if (j > 0)
						n_printf(", %s", listArg->GetS());
					else
						n_printf("%s", listArg->GetS());
				}
				break;
			}
		case nArg::ARGTYPE_CODE:
			{
				lua_pushstring(L, listArg->GetC());
				break;
			}
		case nArg::ARGTYPE_BOOL:
			{
				lua_pushboolean(L, listArg->GetB());
				if (print)
				{
					if (j > 0)
						n_printf(", %s", listArg->GetB() ? "true" : "false");
					else
						n_printf("%s", listArg->GetB() ? "true" : "false");
				}
				break;
			}
		case nArg::ARGTYPE_OBJECT:
			{
				nRoot* o = (nRoot*)listArg->GetO();
				if (o)
				{
					nLuaServer::Instance->ThunkNebObject(L, o);
					if (print)
					{						
						if (j > 0)
							n_printf(", %s", o->GetFullName().c_str());
						else
							n_printf("%s", o->GetFullName().c_str());
					}
				}
				else
				{
					lua_pushnil(L);
					if (print)
					{
						if (j > 0)
							n_printf(", nil");
						else
							n_printf("nil");
					}
				}
				break;
			}
		case nArg::ARGTYPE_LIST:
			{
				if (print && (j > 0))
					n_printf(", ");
				nLuaServer::ListArgToTable(L, listArg, print);
			}
		default:
			lua_pushnil(L);
		}
		lua_settable(L, -3);
		listArg++;
	}

	if (print)
		n_printf( "}" );
}

//--------------------------------------------------------------------
/**
@brief Pushes the output args of a cmd onto the LUA stack, and 
optionally prints the output.
*/
void nLuaServer::OutArgsToStack(lua_State* L, nCmd* cmd, bool print)
{
	cmd->Rewind();
	nArg* narg;
	int n = cmd->GetNumOutArgs();
	// loop through and pack it all in
	for (int i = 0; i < n; i++)
	{
		// get the arg prepped
		narg = cmd->Out();
		switch (narg->GetType())
		{
		case nArg::ARGTYPE_INT:
			{
				lua_pushnumber(L, narg->GetI());
				if (print)
				{
					if (i > 0)
						n_printf(" %d", narg->GetI());
					else
						n_printf("%d", narg->GetI());
				}
				break;
			}
		case nArg::ARGTYPE_FLOAT:
			{
				lua_pushnumber(L, narg->GetF());
				if (print)
				{
					if (i > 0)
						n_printf(" %f", narg->GetF());
					else
						n_printf("%f", narg->GetF());
				}
				break;
			}
		case nArg::ARGTYPE_STRING:
			{
				lua_pushstring(L, narg->GetS());
				if (print)
				{
					if (i > 0)
						n_printf(" %s", narg->GetS());
					else
						n_printf("%s", narg->GetS());
				}
				break;
			}
		case nArg::ARGTYPE_CODE:
			{
				lua_pushstring(L, narg->GetC());
				break;
			}
		case nArg::ARGTYPE_BOOL:
			{
				lua_pushboolean(L, narg->GetB());
				if (print)
				{
					if (i > 0)
						n_printf(" %s", narg->GetB() ? "true" : "false");
					else
						n_printf("%s", narg->GetB() ? "true" : "false");
				}
				break;
			}
		case nArg::ARGTYPE_OBJECT:
			{
				nRoot* o = (nRoot*)narg->GetO();
				if (o)
				{
					nLuaServer::Instance->ThunkNebObject(L, o);
					if (print)
					{						
						if (i > 0)
							n_printf(" %s", o->GetFullName().c_str());
						else
							n_printf("%s", o->GetFullName().c_str());
					}
				}
				else
				{
					lua_pushnil(L);
					if (print)
					{
						if (i > 0)
							n_printf(" nil");
						else
							n_printf("nil");
					}
				}
				break;
			}
		case nArg::ARGTYPE_VOID:
			{
				lua_pushnil(L);
				break;
			}
		case nArg::ARGTYPE_LIST:
			{
				nLuaServer::ListArgToTable(L, narg, print);
				break;
			}
		}
	}

	if (print && (n > 0))
		n_printf("\n");
}

//--------------------------------------------------------------------
/**
@brief Take a cmd's in-args and put them on the LUA stack.
*/
void nLuaServer::InArgsToStack( lua_State* L, nCmd* cmd )
{
	cmd->Rewind();
	for (int i = 0; i < cmd->GetNumInArgs(); i++)
		nLuaServer::ArgToStack(L, cmd->In()); 
}

//--------------------------------------------------------------------
/**
@brief Take a cmd's out-args from the stack and pack them into
the cmd.
*/
bool nLuaServer::StackToOutArgs(lua_State* L, nCmd* cmd)
{
	cmd->Rewind();
	for (int i = -cmd->GetNumOutArgs(); i < 0 ; i++)
		if (!nLuaServer::StackToArg(L, cmd->Out(), i))
			return false;
	return true;
}

//--------------------------------------------------------------------
/**
@brief Convert an nArg to a Lua compatible value and push it on the 
LUA stack.
*/
void nLuaServer::ArgToStack(lua_State* L, nArg* arg)
{
	switch (arg->GetType())
	{
	case nArg::ARGTYPE_VOID:
		lua_pushnil(L);
		break;

	case nArg::ARGTYPE_INT:
		lua_pushnumber(L, arg->GetI());
		break;

	case nArg::ARGTYPE_FLOAT:
		lua_pushnumber(L, arg->GetF());
		break;

	case nArg::ARGTYPE_STRING:
		lua_pushstring(L, arg->GetS());
		break;

	case nArg::ARGTYPE_CODE:
		lua_pushstring(L, arg->GetC());
		break;

	case nArg::ARGTYPE_BOOL:
		lua_pushboolean(L, arg->GetB());
		break;

	case nArg::ARGTYPE_OBJECT:
		nLuaServer::ThunkNebObject(L, (nRoot*)arg->GetO());
		break;

	case nArg::ARGTYPE_LIST:
		nLuaServer::ListArgToTable(L, arg, false);
		break;
	}
}

//--------------------------------------------------------------------
/**
@brief Convert a value from the LUA stack to an nArg of the 
specified type.
@param index The index of the value to convert.
@return True if arg was successfuly retrieved and converted.
*/
bool nLuaServer::StackToArg(lua_State* L, nArg* arg, int index)
{
	switch (arg->GetType())
	{
	case nArg::ARGTYPE_INT:
		{
			if (!lua_isnumber(L, index)) 
				return false;
			arg->SetI(lua_tonumber(L, index));
			break;
		}
	case nArg::ARGTYPE_FLOAT:
		{
			if (!lua_isnumber(L, index)) 
				return false;
			arg->SetF(lua_tonumber(L, index));
			break;
		}
	case nArg::ARGTYPE_STRING:
		{
			if (!lua_isstring(L, index)) 
				return false;
			arg->SetS(lua_tostring(L, index));
			break;
		}
	case nArg::ARGTYPE_CODE:
		{
			if (!lua_isstring(L, index)) 
				return false;
			arg->SetC(lua_tostring(L, index));
			break;
		}
	case nArg::ARGTYPE_BOOL:
		{
			if (!lua_isboolean(L, index)) 
				return false;
			arg->SetB(lua_toboolean(L, index) );
			break;
		}
	case nArg::ARGTYPE_OBJECT:
		{
			if (lua_isuserdata(L, index))
			{
				arg->SetO(lua_touserdata(L, index));
			}
			else if (lua_istable(L, index))
			{
				nRoot* root = nLuaServer::UnpackThunkRoot(L, index);
				if (!root) 
					return false;
				arg->SetO(root);
			}
			else
			{
				return false;
			}
			break;
		}
	case nArg::ARGTYPE_LIST:
		{
			n_message("List nArgs can't be used as input args.");
			return false;
			break;
		}
	default:
		return false;
	}
	return true;
}


//--------------------------------------------------------------------
//  EOF
//--------------------------------------------------------------------

