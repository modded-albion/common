/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

/*
 * The interface between the mod loader (version.dll) and a mod DLL in the folder `mods` of the game.
 *
 * The loader and each mod are different modules. Each one has its own C runtime, its own heap and its own copy of
 * the C++ standard library. Thus only plain C types go across this interface: integers, doubles, pointers to
 * functions and to text. A text that goes across is UTF-8 with a terminating zero. The receiver copies a text that
 * it keeps; the pointer is valid only during the call. No C++ exception may go across: each function of a mod and
 * of the loader catches its exceptions.
 *
 * A mod DLL exports these four functions (see `MoaModExports` below):
 * - `MoaModApiVersion` gives the version of this interface that the mod uses. The loader refuses a mod with a
 *   different version, and does not call its other functions.
 * - `MoaModGetInfo` gives the id, the name and the version of the mod.
 * - `MoaModInit` gets the table of the loader. The mod registers its settings, its menu fields, its actions and its
 *   method callbacks. The loader refuses registrations after `MoaModInit` returns. A result of 0 tells that the mod cannot
 * start: the loader then removes all the registrations of the mod, unloads the DLL, writes the reason to the log, and the game
 * continues.
 * - `MoaModShutdown` tells the mod that the game quits or that the loader unloads the mod. The mod puts back each
 *   value of the game that it changed. After the call, the loader calls no function of the mod.
 *
 * Threads: `MoaModInit` runs on a thread of the loader that IL2CPP knows (attached). It must not change Unity objects.
 * The first call of each settings function also runs on that thread, after all inits and before the first frame. All
 * other callbacks of the loader (method callbacks, tasks, later settings, fields, actions, readouts, queries) run on
 * the main thread of the game, between or in the frames. A mod can call `log`, `postTask` and `listQueries` of the
 * table from any thread.
 *
 * The size of the table: the loader can add functions at the end of `MoaHostApi` with no change of the version. A mod
 * checks `size` with `MOA_HOST_API_HAS` before it uses a function after `MOA_HOST_API_FIRST_SIZE`. An older loader
 * gives a smaller table.
 *
 * IL2CPP: each mod finds the `il2cpp_*` exports of GameAssembly.dll itself (the library `moa_mod_game` of common).
 * These exports are the C interface of the game and have no state of the loader. The loader owns each change of a
 * code pointer of the game: a mod never installs a hook itself. It asks the loader for a callback before or after a
 * managed method, and the loader installs one hook for each method and calls the callbacks of all mods in order.
 */

#ifndef MOA_MOD_API_H
#define MOA_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The version of this interface. A change that a mod of the old version cannot use increases it.
#define MOA_MOD_API_VERSION 1u

typedef enum MoaLogLevel
{
	MOA_LOG_DEBUG = 0,
	MOA_LOG_INFO = 1,
	MOA_LOG_WARN = 2,
	MOA_LOG_ERROR = 3,
} MoaLogLevel;

/// When a method callback runs: before or after the original method.
typedef enum MoaHookPhase
{
	MOA_HOOK_BEFORE = 0,
	MOA_HOOK_AFTER = 1,
} MoaHookPhase;

/// The control of a menu field.
typedef enum MoaFieldKind
{
	MOA_FIELD_TOGGLE = 0, ///< A check box. The value is `toggle`.
	MOA_FIELD_NUMBER = 1, ///< A slider. The value is `number`.
	MOA_FIELD_CHOICE = 2, ///< A list of names. The value is `choice`, the index of the name.
} MoaFieldKind;

/// A place for a text that the receiver gives. The sender calls `assign` one time with the text. The receiver copies
/// it.
typedef struct MoaText
{
	void* context;
	void (*assign)(void* context, const char* utf8, size_t size);
} MoaText;

/// The current call of a hooked managed method: the object (`this`) and the parameters. A method with no parameters
/// has nothing in `arguments`.
typedef struct MoaCall
{
	void* instance;
	void* arguments[2];
} MoaCall;

/// A managed method, found by its namespace, its class, its name and the C# full names of its parameters. The method
/// has no return value and no parameters, or two parameters that each fit in one register.
typedef struct MoaMethodDesc
{
	const char* space; ///< The namespace, or "" for the global namespace.
	const char* className;
	const char* method;
	const char* const* parameterTypes;
	uint32_t parameterCount;
} MoaMethodDesc;

/// The value of a field.
typedef struct MoaFieldValue
{
	uint32_t toggle;
	double number;
	uint32_t choice;
} MoaFieldValue;

/// The value of a field now, with the range that a number has now. Some ranges depend on another field.
typedef struct MoaFieldState
{
	MoaFieldValue value;
	double min;
	double max;
	/// The field changes only after a restart, and the value differs from the value that the mod uses now.
	uint32_t restartPending;
} MoaFieldState;

/// A value of the settings that the menu bar shows and changes. The loader copies each text.
typedef struct MoaFieldDesc
{
	const char* menu;    ///< The menu of the menu bar, or null for the menu of the mod.
	const char* group;   ///< The heading of the part of the menu.
	const char* section; ///< The JSON object of the field in mods.json, for example "camera".
	const char* key;     ///< The name of the field in its JSON object.
	const char* label;
	const char* help;
	MoaFieldKind kind;
	double min;         ///< Numbers only.
	double max;         ///< Numbers only.
	double step;        ///< Numbers only.
	const char* format; ///< The printf format of a number.
	uint32_t logarithmic;
	uint32_t restartOnly; ///< The change works only after a restart of the game.
	const char* const* choices;
	uint32_t choiceCount;
} MoaFieldDesc;

/// Something that the user does with a button of the menu bar, with a hotkey, or with both.
typedef struct MoaActionDesc
{
	const char* menu;  ///< The menu of the button, or null for the menu of the mod.
	const char* group; ///< The heading of the part of the menu.
	const char* label; ///< The text of the button, or null for an action with no button.
	const char* help;
	/// The name of the hotkey in the object `hotkeys` of mods.json, for example "toggleCamera", or null for no hotkey.
	const char* hotkey;
	/// The hotkey when mods.json does not give one, for example "Ctrl+Shift+F5", or null for none.
	const char* defaultHotkey;
} MoaActionDesc;

typedef void (*MoaCallFn)(void* user, const MoaCall* call);
typedef void (*MoaTaskFn)(void* user);
typedef uint32_t (*MoaSettingsFn)(void* user, const char* json, MoaText* error);
typedef void (*MoaFieldGetFn)(void* user, MoaFieldState* state);
typedef uint32_t (*MoaFieldSetFn)(void* user, const MoaFieldValue* value, uint32_t save, MoaText* message);
typedef uint32_t (*MoaEnabledFn)(void* user);
typedef void (*MoaReadFn)(void* user, MoaText* text);
/// A query of the inspector. `params` is a JSON object. Return 1 and give the answer as JSON text to `result`, or
/// return 0 and give an error message to `result`.
typedef uint32_t (*MoaQueryFn)(void* user, const char* params, MoaText* result);

/// The table that the loader gives to `MoaModInit`. It is valid until `MoaModShutdown` returns. Each function gets
/// `context` as its first parameter. A function that registers something returns 1, or 0 if it refuses.
typedef struct MoaHostApi
{
	uint32_t apiVersion; ///< MOA_MOD_API_VERSION of the loader.
	uint32_t size;       ///< sizeof(MoaHostApi) of the loader.
	void* context;

	const char* loaderVersion;
	const char* buildGuid;      ///< The build guid of the game (moa_Data/boot.config).
	const char* gameFolder;     ///< The folder of moa.exe. A mod never writes into it.
	const char* settingsFolder; ///< The folder of mods.json, outside the game folder.
	const char* logFolder;      ///< The folder of the log files, outside the game folder.

	/// Write a line to the log of the loader. Any thread.
	void (*log)(void* context, MoaLogLevel level, const char* text);

	/// Call `callback` before or after each call of the managed method. `name` must be different for each callback of
	/// the mod on one method.
	uint32_t (*addMethodCallback)(void* context, const MoaMethodDesc* method, MoaHookPhase phase, const char* name,
	                              MoaCallFn callback, void* user);
	/// Run `task` one time on the main thread, between two frames. Any thread can post a task.
	uint32_t (*postTask)(void* context, MoaTaskFn task, void* user);

	/// Give the JSON object `section` of mods.json to `load` now and after each change of the file. An object that is
	/// not in the file is "{}". `load` returns 0 and an error to refuse the settings: the mod then keeps its settings.
	uint32_t (*addSettings)(void* context, const char* section, MoaSettingsFn load, void* user);
	/// Write a change (a JSON merge patch of the full file) to mods.json, for example after a hotkey.
	uint32_t (*saveSettings)(void* context, const char* patch);

	/// Show a field in the menu bar. `get` gives its value; `set` changes it and gives a message, or returns 0 and an
	/// error. With `save` 1, the loader writes the new value to mods.json.
	uint32_t (*addField)(void* context, const MoaFieldDesc* field, MoaFieldGetFn get, MoaFieldSetFn set, void* user);
	/// Add an action. `enabled` can be null (always enabled).
	uint32_t (*addAction)(void* context, const MoaActionDesc* action, MoaTaskFn run, MoaEnabledFn enabled, void* user);
	/// Show lines of text in the menu bar, for example the phase of the moon. `read` runs on the main thread.
	uint32_t (*addReadout)(void* context, const char* menu, const char* group, MoaReadFn read, void* user);
	/// The status of the mod as a JSON object, for the menu bar and the log.
	uint32_t (*setStatusReader)(void* context, MoaReadFn read, void* user);

	/// Show a short message on the screen of the game (and in the log).
	void (*showMessage)(void* context, const char* text, double seconds);

	/* The functions below are not in the first loader of version 1. Check them with MOA_HOST_API_HAS. */

	/// Add a query that the inspector can call, for example "sky.state". Each query name has one owner. `help` is a
	/// short text for the list of the queries, or null. The loader calls `query` on the main thread.
	uint32_t (*addQuery)(void* context, const char* name, const char* help, MoaQueryFn query, void* user);
	/// Give the queries of the loader and of all mods to `list`: a JSON array of objects with "name", "help" and "mod".
	/// Any thread. The list is complete after the inits of all mods.
	void (*listQueries)(void* context, MoaText* list);
	/// Call the query `name` with the JSON object `params` (or null for no parameters). Main thread only. Return 1 and
	/// give the answer as JSON text to `result`, or return 0 and give an error message to `result`.
	uint32_t (*callQuery)(void* context, const char* name, const char* params, MoaText* result);
} MoaHostApi;

/// The size of the table of the first loader of version 1. Each loader of version 1 gives at least this size.
#define MOA_HOST_API_FIRST_SIZE offsetof(MoaHostApi, addQuery)

/// 1 if the table `api` (a pointer) has the function `member`, else 0.
#define MOA_HOST_API_HAS(api, member) ((api)->size >= offsetof(MoaHostApi, member) + sizeof((api)->member) ? 1 : 0)

/// What a mod tells about itself. The texts stay valid while the DLL is loaded.
typedef struct MoaModInfo
{
	const char* id;   ///< Letters, digits, '-' and '_', for example "improved_camera".
	const char* name; ///< The name in the menu bar.
	const char* version;
} MoaModInfo;

/// The names of the exports of a mod DLL.
#define MOA_MOD_EXPORT_API_VERSION "MoaModApiVersion"
#define MOA_MOD_EXPORT_GET_INFO "MoaModGetInfo"
#define MOA_MOD_EXPORT_INIT "MoaModInit"
#define MOA_MOD_EXPORT_SHUTDOWN "MoaModShutdown"

typedef uint32_t (*MoaModApiVersionFn)(void);
typedef const MoaModInfo* (*MoaModGetInfoFn)(void);
typedef uint32_t (*MoaModInitFn)(const MoaHostApi* host, uint32_t apiVersion);
typedef void (*MoaModShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif
