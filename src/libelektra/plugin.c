/***************************************************************************
          plugin.c  -  Everything related to a plugin
                             -------------------
 *  begin                : Wed 19 May, 2010
 *  copyright            : (C) 2010 by Markus Raab
 *  email                : elektra@markus-raab.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the BSD License (revised).                      *
 *                                                                         *
 ***************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if DEBUG && HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#include <kdbinternal.h>

/**
 * Load a plugin.
 *
 * The array of plugins must be set to 0.
 * Its length is 10.
 *
 * @return -1 on failure
 */
int processPlugins(Plugin **plugins, KeySet *config)
{
	Key *root;
	Key *cur;

	ksRewind (config);

	root = ksNext(config);

	while ((cur = ksNext(config)) != 0)
	{
		if (keyRel (root, cur) == 1)
		{
			// this describes a plugin!
			const char *fullname = keyBaseName(cur);
			const char *pluginname = 0;
			int pluginnumber = 0;
			if (fullname[0] != '#')
			{
#if DEBUG
				printf ("Names of Plugins must start with a #\n");
#endif
				goto error;
			}
			if (fullname[1] < '0' || fullname[1] > '9')
			{
#if DEBUG
				printf ("Names of Plugins must start have the position number as second char\n");
#endif
				goto error;
			}
			pluginnumber = fullname[1]-'0';
			pluginname = &fullname[2];

			plugins[pluginnumber] = pluginOpen(pluginname, 0);
		}
	}

	ksDel (config);
	return 0;

error:
	ksDel (config);
	return -1;
}


Plugin* pluginOpen(const char *pluginname, KeySet *config)
{
	Plugin* handle;
	char* plugin_name;

	kdbLibHandle dlhandle=0;
	typedef KDB *(*KDBPluginFactory) (void);
	KDBPluginFactory kdbPluginFactory=0;

	plugin_name = malloc(sizeof("libelektra-")+strlen(pluginname));

	strncpy(plugin_name,"libelektra-",sizeof("libelektra-"));
	strncat(plugin_name,pluginname,strlen(pluginname));

	dlhandle=kdbLibLoad(plugin_name);
	if (dlhandle == 0) {
		/*errno=KDB_ERR_EBACKEND;*/
#if DEBUG && VERBOSE
		printf("kdbLibLoad(%s) failed\n", plugin_name);
#endif
		goto err_clup; /* error */
	}

	/* load the "kdbPluginFactory" symbol from plugin */
	kdbPluginFactory=(KDBPluginFactory)kdbLibSym(dlhandle, "kdbPluginFactory");
	if (kdbPluginFactory == 0) {
		/*errno=KDB_ERR_NOSYS;*/
#if DEBUG && VERBOSE
		printf("Could not kdbLibSym kdbPluginFactory for %s\n", plugin_name);
#endif
		goto err_clup; /* error */
	}

	/*TODO: kdbPluginFactory should return plugin*/
	handle=(Plugin*)kdbPluginFactory();
	if (handle == 0)
	{
		/*errno=KDB_ERR_NOSYS;*/
#if DEBUG && VERBOSE
		printf("Could not call kdbPluginFactory for %s\n", plugin_name);
#endif
		goto err_clup; /* error */
	}

	/* save the libloader handle for future use */
	handle->dlHandle=dlhandle;

	/* let the plugin initialize itself */
	if (handle->kdbOpen)
	{
		handle->config = config;
		// TODO should be plugin
		if ((handle->kdbOpen((KDB*)handle)) == -1)
		{
#if DEBUG && VERBOSE
			printf("kdbOpen() failed for %s\n", plugin_name);
#endif
		}
	}
	else {
		/*errno=KDB_ERR_NOSYS;*/
#if DEBUG && VERBOSE
			printf("No kdbOpen supplied in %s\n", plugin_name);
#endif
		goto err_clup;
	}

#if DEBUG && VERBOSE
	printf("Finished loading Plugin %s\n", plugin_name);
#endif
	free(plugin_name);
	return handle;

err_clup:
#if DEBUG
	printf("Failed to load plugin %s\n", plugin_name);
#endif
	free(plugin_name);
	return 0;
}

int pluginClose(Plugin *handle)
{
	int rc=0;

	if (!handle) return 0;

	if (handle->kdbClose)
	{
		// TODO should be plugin
		rc=handle->kdbClose((KDB*)handle);
	}
	
	if (rc == 0) {
		kdbLibClose(handle->dlHandle);
		if (handle->config) ksDel(handle->config);
		free(handle);
	}
	
	return rc;
}



/**
 * This function must be called by a plugin's kdbPluginFactory() to
 * define the plugin's methods that will be exported.
 *
 * See KDBEXPORT() how to use it for plugins.
 *
 * The order and number of arguments are flexible (as in keyNew() and ksNew()) to let
 * libelektra.so evolve without breaking its ABI compatibility with plugins.
 * So for each method a plugin must export, there is a flag defined by
 * #plugin_t. Each flag tells kdbPluginExport() which method comes
 * next. A plugin can have no implementation for a few methods that have
 * default inefficient high-level implementations and to use these defaults, simply
 * don't pass anything to kdbPluginExport() about them.
 *
 * @param pluginName a simple name for this plugin
 * @return an object that contains all plugin informations needed by
 * 	libelektra.so
 * @ingroup plugin
 */
Plugin *pluginExport(const char *pluginName, ...) {
	va_list va;
	Plugin *returned;
	plugin_t method=0;

	if (pluginName == 0) return 0;

	returned=kdbiCalloc(sizeof(struct _Plugin));

	/* Start processing parameters */
	
	va_start(va,pluginName);

	while ((method=va_arg(va,plugin_t))) {
		switch (method) {
			case KDB_PLUGIN_OPEN:
				returned->kdbOpen=va_arg(va,kdbOpenPtr);
				break;
			case KDB_PLUGIN_CLOSE:
				returned->kdbClose=va_arg(va,kdbClosePtr);
				break;
			case KDB_PLUGIN_GET:
				returned->kdbGet=va_arg(va,kdbGetPtr);
				break;
			case KDB_PLUGIN_SET:
				returned->kdbSet=va_arg(va,kdbSetPtr);
				break;
				/*
			case KDB_PLUGIN_VERSION:
				returned->capability->version=va_arg(va, char *);
				break;
			case KDB_PLUGIN_DESCRIPTION:
				returned->capability->description=va_arg(va, char *);
				break;
			case KDB_PLUGIN_AUTHOR:
				returned->capability->author=va_arg(va, char *);
				break;
			case KDB_PLUGIN_LICENCE:
				returned->capability->licence=va_arg(va, char *);
				break;
				*/
			default:
#if DEBUG
				printf ("plugin passed something unexpected\n");
#endif
			case KDB_PLUGIN_END:
				va_end(va);
				return returned;
		}
	}
	return returned;
}



/**
 * Returns the configuration of that plugin.
 */
KeySet *pluginGetConfig(Plugin *handle)
{
	return handle->config;
}

void *pluginGetData(Plugin *handle)
{
	return handle->data;
}

void pluginSetData(Plugin *handle, void *data)
{
	handle->data = data;
}
