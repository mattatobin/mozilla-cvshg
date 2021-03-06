/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Josh Aas <josh@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// TODO: Implement Java callbacks

#include "prtypes.h"
#include "prmem.h"
#include "prclist.h"
#include "nsAutoLock.h"
#include "ns4xPlugin.h"
#include "ns4xPluginInstance.h"
#include "ns4xPluginStreamListener.h"
#include "nsIServiceManager.h"
#include "nsThreadUtils.h"

#include "nsIMemory.h"
#include "nsIPluginStreamListener.h"
#include "nsPluginsDir.h"
#include "nsPluginSafety.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsPluginLogging.h"

#include "nsIPluginInstancePeer2.h"
#include "nsIJSContextStack.h"

#include "nsPIPluginInstancePeer.h"
#include "nsIDOMElement.h"
#include "nsIDOMDocument.h"
#include "nsPIDOMWindow.h"
#include "nsIDocument.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "nsDOMJSUtils.h"
#include "nsIPrincipal.h"

#include "jscntxt.h"

#include "nsIXPConnect.h"

#include "nsIObserverService.h"
#include <prinrval.h>

#if defined(XP_MACOSX)
#include <Resources.h>
#endif

//needed for nppdf plugin
#ifdef MOZ_WIDGET_GTK2
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include "gtk2xtbin.h"
#endif

#include "nsJSNPRuntime.h"

static PRLock *sPluginThreadAsyncCallLock = nsnull;
static PRCList sPendingAsyncCalls = PR_INIT_STATIC_CLIST(&sPendingAsyncCalls);

// POST/GET stream type
enum eNPPStreamTypeInternal {
  eNPPStreamTypeInternal_Get,
  eNPPStreamTypeInternal_Post
};

////////////////////////////////////////////////////////////////////////
// CID's && IID's
static NS_DEFINE_IID(kCPluginManagerCID, NS_PLUGINMANAGER_CID);
static NS_DEFINE_IID(kPluginManagerCID, NS_PLUGINMANAGER_CID);
static NS_DEFINE_IID(kMemoryCID, NS_MEMORY_CID);

PR_BEGIN_EXTERN_C

  ////////////////////////////////////////////////////////////////////////
  // Static stub functions that are exported to the 4.x plugin as entry
  // points via the CALLBACKS variable.
  //
  static NPError NP_CALLBACK
  _requestread(NPStream *pstream, NPByteRange *rangeList);

  static NPError NP_CALLBACK
  _geturlnotify(NPP npp, const char* relativeURL, const char* target,
                void* notifyData);

  static NPError NP_CALLBACK
  _getvalue(NPP npp, NPNVariable variable, void *r_value);

  static NPError NP_CALLBACK
  _setvalue(NPP npp, NPPVariable variable, void *r_value);

  static NPError NP_CALLBACK
  _geturl(NPP npp, const char* relativeURL, const char* target);

  static NPError NP_CALLBACK
  _posturlnotify(NPP npp, const char* relativeURL, const char *target,
                 uint32 len, const char *buf, NPBool file, void* notifyData);

  static NPError NP_CALLBACK
  _posturl(NPP npp, const char* relativeURL, const char *target, uint32 len,
              const char *buf, NPBool file);

  static NPError NP_CALLBACK
  _newstream(NPP npp, NPMIMEType type, const char* window, NPStream** pstream);

  static int32 NP_CALLBACK
  _write(NPP npp, NPStream *pstream, int32 len, void *buffer);

  static NPError NP_CALLBACK
  _destroystream(NPP npp, NPStream *pstream, NPError reason);

  static void NP_CALLBACK
  _status(NPP npp, const char *message);

  static void NP_CALLBACK
  _memfree (void *ptr);

  static uint32 NP_CALLBACK
  _memflush(uint32 size);

  static void NP_CALLBACK
  _reloadplugins(NPBool reloadPages);

  static void NP_CALLBACK
  _invalidaterect(NPP npp, NPRect *invalidRect);

  static void NP_CALLBACK
  _invalidateregion(NPP npp, NPRegion invalidRegion);

  static void NP_CALLBACK
  _forceredraw(NPP npp);

  static void NP_CALLBACK
  _pushpopupsenabledstate(NPP npp, NPBool enabled);

  static void NP_CALLBACK
  _poppopupsenabledstate(NPP npp);

  typedef void(*PluginThreadCallback)(void *);
  static void NP_CALLBACK
  _pluginthreadasynccall(NPP instance, PluginThreadCallback func,
                         void *userData);

  static const char* NP_CALLBACK
  _useragent(NPP npp);

  static void* NP_CALLBACK
  _memalloc (uint32 size);

#ifdef OJI
  static JRIEnv* NP_CALLBACK
  _getJavaEnv(void);

#if 1

  static jref NP_CALLBACK
  _getJavaPeer(NPP npp);

#endif
#endif /* OJI */

PR_END_EXTERN_C

#if defined(XP_MACOSX) && defined(__POWERPC__)

#define TV2FP(tvp) _TV2FP((void *)tvp)

static void*
_TV2FP(void *tvp)
{
    static uint32 glue[6] = {
      0x3D800000, 0x618C0000, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420
    };
    uint32* newGlue = NULL;

    if (tvp != NULL) {
        newGlue = (uint32*) malloc(sizeof(glue));
        if (newGlue != NULL) {
            memcpy(newGlue, glue, sizeof(glue));
            newGlue[0] |= ((UInt32)tvp >> 16);
            newGlue[1] |= ((UInt32)tvp & 0xFFFF);
            MakeDataExecutable(newGlue, sizeof(glue));
        }
    }
    return newGlue;
}

#define FP2TV(fp) _FP2TV((void *)fp)

static void*
_FP2TV(void *fp)
{
    void **newGlue = NULL;
    if (fp != NULL) {
        newGlue = (void**) malloc(2 * sizeof(void *));
        if (newGlue != NULL) {
            newGlue[0] = fp;
            newGlue[1] = NULL;
        }
    }
    return newGlue;
}

#else

#define TV2FP(f) (f)
#define FP2TV(f) (f)

#endif /* XP_MACOSX && __POWERPC__ */

// This function sends a notification using the observer service to any object
// registered to listen to the "experimental-notify-plugin-call" subject.
// Each "experimental-notify-plugin-call" notification carries with it the run
// time value in milliseconds that the call took to execute.
void NS_NotifyPluginCall(PRIntervalTime startTime) 
{
  PRIntervalTime endTime = PR_IntervalNow() - startTime;
  nsCOMPtr<nsIObserverService> notifyUIService =
    do_GetService("@mozilla.org/observer-service;1");
  float runTimeInSeconds = float(endTime) / PR_TicksPerSecond();
  nsAutoString runTimeString;
  runTimeString.AppendFloat(runTimeInSeconds);
  const PRUnichar* runTime = runTimeString.get();
  notifyUIService->NotifyObservers(nsnull, "experimental-notify-plugin-call",
                                   runTime);
}

////////////////////////////////////////////////////////////////////////
// Globals
NPNetscapeFuncs ns4xPlugin::CALLBACKS;

////////////////////////////////////////////////////////////////////////
void
ns4xPlugin::CheckClassInitialized(void)
{
  static PRBool initialized = FALSE;

  if (initialized)
    return;

  // XXX It'd be nice to make this const and initialize it statically...
  CALLBACKS.size = sizeof(CALLBACKS);
  CALLBACKS.version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;

  CALLBACKS.geturl =
    NewNPN_GetURLProc(FP2TV(_geturl));

  CALLBACKS.posturl =
    NewNPN_PostURLProc(FP2TV(_posturl));

  CALLBACKS.requestread =
    NewNPN_RequestReadProc(FP2TV(_requestread));

  CALLBACKS.newstream =
    NewNPN_NewStreamProc(FP2TV(_newstream));

  CALLBACKS.write =
    NewNPN_WriteProc(FP2TV(_write));

  CALLBACKS.destroystream =
    NewNPN_DestroyStreamProc(FP2TV(_destroystream));

  CALLBACKS.status =
    NewNPN_StatusProc(FP2TV(_status));

  CALLBACKS.uagent =
    NewNPN_UserAgentProc(FP2TV(_useragent));

  CALLBACKS.memalloc =
    NewNPN_MemAllocProc(FP2TV(_memalloc));

  CALLBACKS.memfree =
    NewNPN_MemFreeProc(FP2TV(_memfree));

  CALLBACKS.memflush =
    NewNPN_MemFlushProc(FP2TV(_memflush));

  CALLBACKS.reloadplugins =
    NewNPN_ReloadPluginsProc(FP2TV(_reloadplugins));

#ifdef OJI
  CALLBACKS.getJavaEnv =
    NewNPN_GetJavaEnvProc(FP2TV(_getJavaEnv));

  CALLBACKS.getJavaPeer =
    NewNPN_GetJavaPeerProc(FP2TV(_getJavaPeer));
#endif

  CALLBACKS.geturlnotify =
    NewNPN_GetURLNotifyProc(FP2TV(_geturlnotify));

  CALLBACKS.posturlnotify =
    NewNPN_PostURLNotifyProc(FP2TV(_posturlnotify));

  CALLBACKS.getvalue =
    NewNPN_GetValueProc(FP2TV(_getvalue));

  CALLBACKS.setvalue =
    NewNPN_SetValueProc(FP2TV(_setvalue));

  CALLBACKS.invalidaterect =
    NewNPN_InvalidateRectProc(FP2TV(_invalidaterect));

  CALLBACKS.invalidateregion =
    NewNPN_InvalidateRegionProc(FP2TV(_invalidateregion));

  CALLBACKS.forceredraw =
    NewNPN_ForceRedrawProc(FP2TV(_forceredraw));

  CALLBACKS.getstringidentifier =
    NewNPN_GetStringIdentifierProc(FP2TV(_getstringidentifier));

  CALLBACKS.getstringidentifiers =
    NewNPN_GetStringIdentifiersProc(FP2TV(_getstringidentifiers));

  CALLBACKS.getintidentifier =
    NewNPN_GetIntIdentifierProc(FP2TV(_getintidentifier));

  CALLBACKS.identifierisstring =
    NewNPN_IdentifierIsStringProc(FP2TV(_identifierisstring));

  CALLBACKS.utf8fromidentifier =
    NewNPN_UTF8FromIdentifierProc(FP2TV(_utf8fromidentifier));

  CALLBACKS.intfromidentifier =
    NewNPN_IntFromIdentifierProc(FP2TV(_intfromidentifier));

  CALLBACKS.createobject =
    NewNPN_CreateObjectProc(FP2TV(_createobject));

  CALLBACKS.retainobject =
    NewNPN_RetainObjectProc(FP2TV(_retainobject));

  CALLBACKS.releaseobject =
    NewNPN_ReleaseObjectProc(FP2TV(_releaseobject));

  CALLBACKS.invoke =
    NewNPN_InvokeProc(FP2TV(_invoke));

  CALLBACKS.invokeDefault =
    NewNPN_InvokeDefaultProc(FP2TV(_invokeDefault));

  CALLBACKS.evaluate =
    NewNPN_EvaluateProc(FP2TV(_evaluate));

  CALLBACKS.getproperty =
    NewNPN_GetPropertyProc(FP2TV(_getproperty));

  CALLBACKS.setproperty =
    NewNPN_SetPropertyProc(FP2TV(_setproperty));

  CALLBACKS.removeproperty =
    NewNPN_RemovePropertyProc(FP2TV(_removeproperty));

  CALLBACKS.hasproperty =
    NewNPN_HasPropertyProc(FP2TV(_hasproperty));

  CALLBACKS.hasmethod =
    NewNPN_HasMethodProc(FP2TV(_hasmethod));

  CALLBACKS.enumerate =
    NewNPN_EnumerateProc(FP2TV(_enumerate));

  CALLBACKS.construct =
    NewNPN_ConstructProc(FP2TV(_construct));

  CALLBACKS.releasevariantvalue =
    NewNPN_ReleaseVariantValueProc(FP2TV(_releasevariantvalue));

  CALLBACKS.setexception =
    NewNPN_SetExceptionProc(FP2TV(_setexception));

  CALLBACKS.pushpopupsenabledstate =
    NewNPN_PushPopupsEnabledStateProc(FP2TV(_pushpopupsenabledstate));

  CALLBACKS.poppopupsenabledstate =
    NewNPN_PopPopupsEnabledStateProc(FP2TV(_poppopupsenabledstate));

  CALLBACKS.pluginthreadasynccall =
    NewNPN_PluginThreadAsyncCallProc(FP2TV(_pluginthreadasynccall));

  if (!sPluginThreadAsyncCallLock) {
    sPluginThreadAsyncCallLock =
      nsAutoLock::NewLock("sPluginThreadAsyncCallLock");
  }

  initialized = TRUE;

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,("NPN callbacks initialized\n"));
}


////////////////////////////////////////////////////////////////////////
// nsISupports stuff
NS_IMPL_ISUPPORTS2(ns4xPlugin, nsIPlugin, nsIFactory)

ns4xPlugin::ns4xPlugin(NPPluginFuncs* callbacks, PRLibrary* aLibrary,
                       NP_PLUGINSHUTDOWN aShutdown,
                       nsIServiceManagerObsolete* serviceMgr)
{
  memset((void*) &fCallbacks, 0, sizeof(fCallbacks));
  fLibrary = nsnull;

#if defined(XP_WIN) || defined(XP_OS2)
  // On Windows (and Mac) we need to keep a direct reference to the
  // fCallbacks and NOT just copy the struct. See Bugzilla 85334

  NP_GETENTRYPOINTS pfnGetEntryPoints =
    (NP_GETENTRYPOINTS)PR_FindSymbol(aLibrary, "NP_GetEntryPoints");

  if (!pfnGetEntryPoints)
    return;

  fCallbacks.size = sizeof(fCallbacks);

  nsresult result = pfnGetEntryPoints(&fCallbacks);
  NS_ASSERTION(result == NS_OK, "Failed to get callbacks");

  NS_ASSERTION(HIBYTE(fCallbacks.version) >= NP_VERSION_MAJOR,
               "callback version is less than NP version");

  fShutdownEntry = (NP_PLUGINSHUTDOWN)PR_FindSymbol(aLibrary, "NP_Shutdown");
#elif defined(XP_MACOSX)
  NPPluginFuncs np_callbacks;
  memset((void*) &np_callbacks, 0, sizeof(np_callbacks));
  np_callbacks.size = sizeof(np_callbacks);

#ifdef MACOSX_GETENTRYPOINT_SUPPORT
  fShutdownEntry = (NP_PLUGINSHUTDOWN)PR_FindSymbol(aLibrary, "NP_Shutdown");
  NP_GETENTRYPOINTS pfnGetEntryPoints = (NP_GETENTRYPOINTS)PR_FindSymbol(aLibrary, "NP_GetEntryPoints");
  NP_PLUGININIT pfnInitialize = (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_Initialize");
  usesGetEntryPoints = (pfnGetEntryPoints && pfnInitialize && fShutdownEntry);

  if (usesGetEntryPoints) {
    // we call NP_Initialize before getting function pointers to match
    // WebKit's behavior. They implemented this first on Mac OS X.
    if (pfnInitialize(&(ns4xPlugin::CALLBACKS)) != NPERR_NO_ERROR)
      return;
    if (pfnGetEntryPoints(&np_callbacks) != NPERR_NO_ERROR)
      return;
  }
  else
#endif
  {
    // call into the entry point
    NP_MAIN pfnMain = (NP_MAIN)PR_FindSymbol(aLibrary, "main");

    if (pfnMain == NULL)
      return;

    NPError error;
    NPP_ShutdownUPP pfnMainShutdown;
    NS_TRY_SAFE_CALL_RETURN(error,
                            CallNPP_MainEntryProc(pfnMain,
                                                  &(ns4xPlugin::CALLBACKS),
                                                  &np_callbacks,
                                                  &pfnMainShutdown),
                            aLibrary,
                            nsnull);
    
    NPP_PLUGIN_LOG(PLUGIN_LOG_BASIC,
                   ("NPP MainEntryProc called: return=%d\n",error));
    
    if (error != NPERR_NO_ERROR)
      return;
    
    fShutdownEntry = (NP_PLUGINSHUTDOWN)TV2FP(pfnMainShutdown);
    
    // version is a uint16 so cast to int to avoid an invalid
    // comparison due to limited range of the data type
    int cb_version = np_callbacks.version;
    if ((cb_version >> 8) < NP_VERSION_MAJOR)
      return;
  }

  // wrap all plugin entry points tvectors as mach-o callable function
  // pointers.
  fCallbacks.size = sizeof(fCallbacks);
  fCallbacks.version = np_callbacks.version;
  fCallbacks.newp = (NPP_NewUPP) TV2FP(np_callbacks.newp);
  fCallbacks.destroy = (NPP_DestroyUPP) TV2FP(np_callbacks.destroy);
  fCallbacks.setwindow = (NPP_SetWindowUPP) TV2FP(np_callbacks.setwindow);
  fCallbacks.newstream = (NPP_NewStreamUPP) TV2FP(np_callbacks.newstream);
  fCallbacks.destroystream =
    (NPP_DestroyStreamUPP) TV2FP(np_callbacks.destroystream);
  fCallbacks.asfile = (NPP_StreamAsFileUPP) TV2FP(np_callbacks.asfile);
  fCallbacks.writeready = (NPP_WriteReadyUPP) TV2FP(np_callbacks.writeready);
  fCallbacks.write = (NPP_WriteUPP) TV2FP(np_callbacks.write);
  fCallbacks.print = (NPP_PrintUPP) TV2FP(np_callbacks.print);
  fCallbacks.event = (NPP_HandleEventUPP) TV2FP(np_callbacks.event);
  fCallbacks.urlnotify = (NPP_URLNotifyUPP) TV2FP(np_callbacks.urlnotify);
  fCallbacks.getvalue = (NPP_GetValueUPP) TV2FP(np_callbacks.getvalue);
  fCallbacks.setvalue = (NPP_SetValueUPP) TV2FP(np_callbacks.setvalue);
#else // for everyone else
  memcpy((void*) &fCallbacks, (void*) callbacks, sizeof(fCallbacks));
  fShutdownEntry = aShutdown;
#endif

  fLibrary = aLibrary;
}


////////////////////////////////////////////////////////////////////////
ns4xPlugin::~ns4xPlugin(void)
{
  //reset the callbacks list
#if defined(XP_MACOSX) && defined(__POWERPC__)
  // release all wrapped plugin entry points.
  if (fCallbacks.newp)
    free((void *)fCallbacks.newp);
  if (fCallbacks.destroy)
    free((void *)fCallbacks.destroy);
  if (fCallbacks.setwindow)
    free((void *)fCallbacks.setwindow);
  if (fCallbacks.newstream)
    free((void *)fCallbacks.newstream);
  if (fCallbacks.asfile)
    free((void *)fCallbacks.asfile);
  if (fCallbacks.writeready)
    free((void *)fCallbacks.writeready);
  if (fCallbacks.write)
    free((void *)fCallbacks.write);
  if (fCallbacks.print)
    free((void *)fCallbacks.print);
  if (fCallbacks.event)
    free((void *)fCallbacks.event);
  if (fCallbacks.urlnotify)
    free((void *)fCallbacks.urlnotify);
  if (fCallbacks.getvalue)
    free((void *)fCallbacks.getvalue);
  if (fCallbacks.setvalue)
    free((void *)fCallbacks.setvalue);
#endif
  memset((void*) &fCallbacks, 0, sizeof(fCallbacks));
}


#if defined(XP_MACOSX)
////////////////////////////////////////////////////////////////////////
void
ns4xPlugin::SetPluginRefNum(short aRefNum)
{
  fPluginRefNum = aRefNum;
}
#endif


////////////////////////////////////////////////////////////////////////
// Static factory method.
//
///CreatePlugin()
//--------------
//Handles the initialization of old, 4x style plugins.  Creates the ns4xPlugin object.
//One ns4xPlugin object exists per Plugin (not instance).

nsresult
ns4xPlugin::CreatePlugin(nsIServiceManagerObsolete* aServiceMgr,
                         const char* aFileName, const char* aFullPath,
                         PRLibrary* aLibrary, nsIPlugin** aResult)
{
  CheckClassInitialized();

#if defined(XP_UNIX) && !defined(XP_MACOSX)

  ns4xPlugin *plptr;

  NPPluginFuncs callbacks;
  memset((void*) &callbacks, 0, sizeof(callbacks));
  callbacks.size = sizeof(callbacks);

  NP_PLUGINSHUTDOWN pfnShutdown =
    (NP_PLUGINSHUTDOWN)PR_FindFunctionSymbol(aLibrary, "NP_Shutdown");

  // create the new plugin handler
  *aResult = plptr =
    new ns4xPlugin(&callbacks, aLibrary, pfnShutdown, aServiceMgr);

  if (*aResult == NULL)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(*aResult);

  if (!aFileName) //do not call NP_Initialize in this case, bug 74938
    return NS_OK;

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  plptr->Initialize();

  NP_PLUGINUNIXINIT pfnInitialize =
    (NP_PLUGINUNIXINIT)PR_FindFunctionSymbol(aLibrary, "NP_Initialize");

  if (pfnInitialize == NULL)
    return NS_ERROR_UNEXPECTED; // XXX Right error?

  if (pfnInitialize(&(ns4xPlugin::CALLBACKS),&callbacks) != NS_OK)
    return NS_ERROR_UNEXPECTED;

  // now copy function table back to ns4xPlugin instance
  memcpy((void*) &(plptr->fCallbacks), (void*)&callbacks, sizeof(callbacks));
#endif

#ifdef XP_WIN
  // Note: on Windows, we must use the fCallback because plugins may
  // change the function table. The Shockwave installer makes changes
  // in the table while running
  *aResult = new ns4xPlugin(nsnull, aLibrary, nsnull, aServiceMgr);

  if (*aResult == NULL)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(*aResult);

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  if (NS_FAILED((*aResult)->Initialize())) {
    NS_RELEASE(*aResult);
    return NS_ERROR_FAILURE;
  }

  // the NP_Initialize entry point was misnamed as NP_PluginInit,
  // early in plugin project development.  Its correct name is
  // documented now, and new developers expect it to work.  However,
  // I don't want to break the plugins already in the field, so
  // we'll accept either name

  NP_PLUGININIT pfnInitialize =
    (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_Initialize");

  if (!pfnInitialize)
    pfnInitialize = (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_PluginInit");

  if (pfnInitialize == NULL)
    return NS_ERROR_UNEXPECTED; // XXX Right error?

  if (pfnInitialize(&(ns4xPlugin::CALLBACKS)) != NS_OK)
    return NS_ERROR_UNEXPECTED;
#endif

#ifdef XP_OS2
  // create the new plugin handler
  *aResult = new ns4xPlugin(nsnull, aLibrary, nsnull, aServiceMgr);

  if (*aResult == NULL)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(*aResult);

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  if (NS_FAILED((*aResult)->Initialize())) {
    NS_RELEASE(*aResult);
    return NS_ERROR_FAILURE;
  }

  // the NP_Initialize entry point was misnamed as NP_PluginInit,
  // early in plugin project development.  Its correct name is
  // documented now, and new developers expect it to work.  However,
  // I don't want to break the plugins already in the field, so
  // we'll accept either name

  NP_PLUGININIT pfnInitialize =
    (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_Initialize");

  if (!pfnInitialize)
    pfnInitialize = (NP_PLUGININIT)PR_FindSymbol(aLibrary, "NP_PluginInit");

  if (pfnInitialize == NULL)
    return NS_ERROR_UNEXPECTED; // XXX Right error?

  // Fixes problem where the OS/2 native multimedia plugins weren't
  // working on mozilla though did work on 4.x.  Problem is that they
  // expect the current working directory to be the plugins dir.
  // Since these plugins are no longer maintained and they represent
  // the majority of the OS/2 plugin contingency, we'll have to make
  // them work here.

#define MAP_DISKNUM_TO_LETTER(n) ('A' + (n - 1))
#define MAP_LETTER_TO_DISKNUM(c) (toupper(c)-'A'+1)

  unsigned long origDiskNum, pluginDiskNum, logicalDisk;

  char pluginPath[CCHMAXPATH], origPath[CCHMAXPATH];
  strcpy(pluginPath, aFileName);
  char* slash = strrchr(pluginPath, '\\');
  *slash = '\0';

  DosQueryCurrentDisk( &origDiskNum, &logicalDisk );
  pluginDiskNum = MAP_LETTER_TO_DISKNUM(pluginPath[0]);

  origPath[0] = MAP_DISKNUM_TO_LETTER(origDiskNum);
  origPath[1] = ':';
  origPath[2] = '\\';

  ULONG len = CCHMAXPATH-3;
  APIRET rc = DosQueryCurrentDir(0, &origPath[3], &len);
  NS_ASSERTION(NO_ERROR == rc,"DosQueryCurrentDir failed");

  BOOL bChangedDir = FALSE;
  BOOL bChangedDisk = FALSE;
  if (pluginDiskNum != origDiskNum) {
    rc = DosSetDefaultDisk(pluginDiskNum);
    NS_ASSERTION(NO_ERROR == rc,"DosSetDefaultDisk failed");
    bChangedDisk = TRUE;
  }

  if (stricmp(origPath, pluginPath) != 0) {
    rc = DosSetCurrentDir(pluginPath);
    NS_ASSERTION(NO_ERROR == rc,"DosSetCurrentDir failed");
    bChangedDir = TRUE;
  }

  nsresult rv = pfnInitialize(&(ns4xPlugin::CALLBACKS));

  if (bChangedDisk) {
    rc= DosSetDefaultDisk(origDiskNum);
    NS_ASSERTION(NO_ERROR == rc,"DosSetDefaultDisk failed");
  }
  if (bChangedDir) {
    rc = DosSetCurrentDir(origPath);
    NS_ASSERTION(NO_ERROR == rc,"DosSetCurrentDir failed");
  }

  if (!NS_SUCCEEDED(rv)) {
    return NS_ERROR_UNEXPECTED;
  }
#endif

#if defined(XP_MACOSX)
  short appRefNum = ::CurResFile();
  short pluginRefNum;

  nsCOMPtr<nsILocalFile> pluginPath;
  NS_NewNativeLocalFile(nsDependentCString(aFullPath), PR_TRUE,
                        getter_AddRefs(pluginPath));

  nsPluginFile pluginFile(pluginPath);
  pluginRefNum = pluginFile.OpenPluginResource();
  if (pluginRefNum == -1)
    return NS_ERROR_FAILURE;

  ns4xPlugin* plugin = new ns4xPlugin(nsnull, aLibrary, nsnull, aServiceMgr);
  if (plugin == NULL)
    return NS_ERROR_OUT_OF_MEMORY;

  ::UseResFile(appRefNum);
  *aResult = plugin;

  NS_ADDREF(*aResult);
  if (NS_FAILED((*aResult)->Initialize())) {
    NS_RELEASE(*aResult);
    return NS_ERROR_FAILURE;
  }

  plugin->SetPluginRefNum(pluginRefNum);
#endif  // XP_MACOSX

#ifdef XP_BEOS
  // I just copied UNIX version.
  // Makoto Hamanaka <VYA04230@nifty.com>

  ns4xPlugin *plptr;

  NPPluginFuncs callbacks;
  memset((void*) &callbacks, 0, sizeof(callbacks));
  callbacks.size = sizeof(callbacks);

  NP_PLUGINSHUTDOWN pfnShutdown =
    (NP_PLUGINSHUTDOWN)PR_FindSymbol(aLibrary, "NP_Shutdown");

  // create the new plugin handler
  *aResult = plptr =
    new ns4xPlugin(&callbacks, aLibrary, pfnShutdown, aServiceMgr);

  if (*aResult == NULL)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(*aResult);

  // we must init here because the plugin may call NPN functions
  // when we call into the NP_Initialize entry point - NPN functions
  // require that mBrowserManager be set up
  plptr->Initialize();

  NP_PLUGINUNIXINIT pfnInitialize =
    (NP_PLUGINUNIXINIT)PR_FindSymbol(aLibrary, "NP_Initialize");

  if (pfnInitialize == NULL)
    return NS_ERROR_FAILURE;

  if (pfnInitialize(&(ns4xPlugin::CALLBACKS),&callbacks) != NS_OK)
    return NS_ERROR_FAILURE;

  // now copy function table back to ns4xPlugin instance
  memcpy((void*) &(plptr->fCallbacks), (void*)&callbacks, sizeof(callbacks));
#endif

  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
//CreateInstance()
//----------------
//Creates a ns4xPluginInstance object.

nsresult
ns4xPlugin::CreateInstance(nsISupports *aOuter, const nsIID &aIID,
                           void **aResult)
{
  if (aResult == NULL)
    return NS_ERROR_NULL_POINTER;

  *aResult = NULL;

  // XXX This is suspicuous!
  nsRefPtr<ns4xPluginInstance> inst =
    new ns4xPluginInstance(&fCallbacks, fLibrary);

  if (!inst)
    return NS_ERROR_OUT_OF_MEMORY;

  return inst->QueryInterface(aIID, aResult);
}


////////////////////////////////////////////////////////////////////////
nsresult
ns4xPlugin::LockFactory(PRBool aLock)
{
  // Not implemented in simplest case.
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
NS_METHOD
ns4xPlugin::CreatePluginInstance(nsISupports *aOuter, REFNSIID aIID,
                                 const char *aPluginMIMEType, void **aResult)
{
  return CreateInstance(aOuter, aIID, aResult);
}


////////////////////////////////////////////////////////////////////////
nsresult
ns4xPlugin::Initialize(void)
{
  if (nsnull == fLibrary)
    return NS_ERROR_FAILURE;
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
nsresult
ns4xPlugin::Shutdown(void)
{
  NPP_PLUGIN_LOG(PLUGIN_LOG_BASIC,
                 ("NPP Shutdown to be called: this=%p\n", this));

  if (fShutdownEntry != nsnull) {
#if defined(XP_MACOSX)
    CallNPP_ShutdownProc(fShutdownEntry);
    ::CloseResFile(fPluginRefNum);
#else
    NS_TRY_SAFE_CALL_VOID(fShutdownEntry(), fLibrary, nsnull);
#endif

#if defined(XP_MACOSX) && defined(__POWERPC__)
    // release the wrapped plugin function.
    free((void *)fShutdownEntry);
#endif
    fShutdownEntry = nsnull;
  }

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
             ("4xPlugin Shutdown done, this=%p", this));
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
nsresult
ns4xPlugin::GetMIMEDescription(const char* *resultingDesc)
{
  const char* (*npGetMIMEDescription)() =
    (const char* (*)()) PR_FindFunctionSymbol(fLibrary, "NP_GetMIMEDescription");

  *resultingDesc = npGetMIMEDescription ? npGetMIMEDescription() : "";

  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
             ("ns4xPlugin::GetMIMEDescription called: this=%p, result=%s\n",
              this, *resultingDesc));

  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
nsresult
ns4xPlugin::GetValue(nsPluginVariable variable, void *value)
{
  PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("ns4xPlugin::GetValue called: this=%p, variable=%d\n", this, variable));

  NPError (*npGetValue)(void*, nsPluginVariable, void*) =
    (NPError (*)(void*, nsPluginVariable, void*)) PR_FindFunctionSymbol(fLibrary,
                                                                "NP_GetValue");

  if (npGetValue && NPERR_NO_ERROR == npGetValue(nsnull, variable, value)) {
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

// Create a new NPP GET or POST (given in the type argument) url
// stream that may have a notify callback
NPError
MakeNew4xStreamInternal(NPP npp, const char *relativeURL, const char *target,
                        eNPPStreamTypeInternal type,
                        PRBool bDoNotify = PR_FALSE,
                        void *notifyData = nsnull, uint32 len = 0,
                        const char *buf = nsnull, NPBool file = PR_FALSE)
{
  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(npp);

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  NS_ASSERTION(inst != NULL, "null instance");
  if (inst == NULL)
    return NPERR_INVALID_INSTANCE_ERROR;

  nsCOMPtr<nsIPluginManager> pm = do_GetService(kPluginManagerCID);
  NS_ASSERTION(pm, "failed to get plugin manager");
  if (!pm) return NPERR_GENERIC_ERROR;

  nsCOMPtr<nsIPluginStreamListener> listener;
  if (target == nsnull)
    ((ns4xPluginInstance*)inst)->NewNotifyStream(getter_AddRefs(listener),
                                                 notifyData,
                                                 bDoNotify, relativeURL);

  switch (type) {
  case eNPPStreamTypeInternal_Get:
    {
      if (NS_FAILED(pm->GetURL(inst, relativeURL, target, listener)))
        return NPERR_GENERIC_ERROR;
      break;
    }
  case eNPPStreamTypeInternal_Post:
    {
      if (NS_FAILED(pm->PostURL(inst, relativeURL, len, buf, file, target,
                                listener)))
        return NPERR_GENERIC_ERROR;
      break;
    }
  default:
    NS_ASSERTION(0, "how'd I get here");
  }

  return NPERR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////
//
// Static callbacks that get routed back through the new C++ API
//

NPError NP_CALLBACK
_geturl(NPP npp, const char* relativeURL, const char* target)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_geturl called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("NPN_GetURL: npp=%p, target=%s, url=%s\n", (void *)npp, target,
   relativeURL));

  PluginDestructionGuard guard(npp);

  // Block Adobe Acrobat from loading URLs that are not http:, https:,
  // or ftp: URLs if the given target is null.
  if (target == nsnull && relativeURL &&
      (strncmp(relativeURL, "http:", 5) != 0) &&
      (strncmp(relativeURL, "https:", 6) != 0) &&
      (strncmp(relativeURL, "ftp:", 4) != 0)) {
    ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;

    const char *name = nsPluginHostImpl::GetPluginName(inst);

    if (name && strstr(name, "Adobe") && strstr(name, "Acrobat")) {
      return NPERR_NO_ERROR;
    }
  }

  return MakeNew4xStreamInternal (npp, relativeURL, target,
                                  eNPPStreamTypeInternal_Get);
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_geturlnotify(NPP npp, const char* relativeURL, const char* target,
              void* notifyData)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_geturlnotify called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
    ("NPN_GetURLNotify: npp=%p, target=%s, notify=%p, url=%s\n", (void*)npp,
     target, notifyData, relativeURL));

  PluginDestructionGuard guard(npp);

  return MakeNew4xStreamInternal (npp, relativeURL, target,
                                  eNPPStreamTypeInternal_Get, PR_TRUE,
                                  notifyData);
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_posturlnotify(NPP npp, const char *relativeURL, const char *target,
               uint32 len, const char *buf, NPBool file, void *notifyData)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_posturlnotify called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_PostURLNotify: npp=%p, target=%s, len=%d, file=%d, "
                  "notify=%p, url=%s, buf=%s\n",
                  (void*)npp, target, len, file, notifyData, relativeURL,
                  buf));

  PluginDestructionGuard guard(npp);

  return MakeNew4xStreamInternal(npp, relativeURL, target,
                                 eNPPStreamTypeInternal_Post, PR_TRUE,
                                 notifyData, len, buf, file);
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_posturl(NPP npp, const char *relativeURL, const char *target,
         uint32 len, const char *buf, NPBool file)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_posturl called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_PostURL: npp=%p, target=%s, file=%d, len=%d, url=%s, "
                  "buf=%s\n",
                  (void*)npp, target, file, len, relativeURL, buf));

  PluginDestructionGuard guard(npp);

  return MakeNew4xStreamInternal(npp, relativeURL, target,
                                 eNPPStreamTypeInternal_Post, PR_FALSE, nsnull,
                                 len, buf, file);
}


////////////////////////////////////////////////////////////////////////
// A little helper class used to wrap up plugin manager streams (that is,
// streams from the plugin to the browser).

class ns4xStreamWrapper : nsISupports
{
public:
  NS_DECL_ISUPPORTS

protected:
  nsIOutputStream *fStream;
  NPStream        fNPStream;

public:
  ns4xStreamWrapper(nsIOutputStream* stream);
  ~ns4xStreamWrapper();

  void GetStream(nsIOutputStream* &result);
  NPStream* GetNPStream(void) { return &fNPStream; }
};

NS_IMPL_ISUPPORTS1(ns4xStreamWrapper, nsISupports)

ns4xStreamWrapper::ns4xStreamWrapper(nsIOutputStream* stream)
  : fStream(stream)
{
  NS_ASSERTION(stream != NULL, "bad stream");

  fStream = stream;
  NS_ADDREF(fStream);

  memset(&fNPStream, 0, sizeof(fNPStream));
  fNPStream.ndata = (void*) this;
}

ns4xStreamWrapper::~ns4xStreamWrapper(void)
{
  fStream->Close();
  NS_IF_RELEASE(fStream);
}

void
ns4xStreamWrapper::GetStream(nsIOutputStream* &result)
{
  result = fStream;
  NS_IF_ADDREF(fStream);
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_newstream(NPP npp, NPMIMEType type, const char* target, NPStream* *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_newstream called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
  ("NPN_NewStream: npp=%p, type=%s, target=%s\n", (void*)npp,
   (const char *)type, target));

  NPError err = NPERR_INVALID_INSTANCE_ERROR;
  if (npp && npp->ndata) {
    nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

    PluginDestructionGuard guard(inst);

    nsCOMPtr<nsIOutputStream> stream;
    nsCOMPtr<nsIPluginInstancePeer> peer;
    if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) &&
      peer &&
      NS_SUCCEEDED(peer->NewStream((const char*) type, target,
                                   getter_AddRefs(stream)))) {
      ns4xStreamWrapper* wrapper = new ns4xStreamWrapper(stream);
      if (wrapper) {
        (*result) = wrapper->GetNPStream();
        err = NPERR_NO_ERROR;
      } else {
        err = NPERR_OUT_OF_MEMORY_ERROR;
      }
    } else {
      err = NPERR_GENERIC_ERROR;
    }
  }
  return err;
}


////////////////////////////////////////////////////////////////////////
int32 NP_CALLBACK
_write(NPP npp, NPStream *pstream, int32 len, void *buffer)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_write called from the wrong thread\n"));
    return 0;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_Write: npp=%p, url=%s, len=%d, buffer=%s\n", (void*)npp,
                  pstream->url, len, (char*)buffer));

  // negative return indicates failure to the plugin
  if (!npp)
    return -1;

  PluginDestructionGuard guard(npp);

  ns4xStreamWrapper* wrapper = (ns4xStreamWrapper*) pstream->ndata;
  NS_ASSERTION(wrapper != NULL, "null stream");

  if (wrapper == NULL)
    return -1;

  nsIOutputStream* stream;
  wrapper->GetStream(stream);

  PRUint32 count = 0;
  nsresult rv = stream->Write((char *)buffer, len, &count);
  NS_RELEASE(stream);

  if (rv != NS_OK)
    return -1;

  return (int32)count;
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_destroystream(NPP npp, NPStream *pstream, NPError reason)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_write called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_DestroyStream: npp=%p, url=%s, reason=%d\n", (void*)npp,
                  pstream->url, (int)reason));

  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(npp);

  nsCOMPtr<nsIPluginStreamListener> listener =
    do_QueryInterface((nsISupports *)pstream->ndata);

  // DestroyStream can kill two kinds of streams: NPP derived and NPN derived.
  // check to see if they're trying to kill a NPP stream
  if (listener) {
    // Tell the stream listner that the stream is now gone.
    listener->OnStopBinding(nsnull, NS_BINDING_ABORTED);

    // FIXME: http://bugzilla.mozilla.org/show_bug.cgi?id=240131
    //
    // Is it ok to leave pstream->ndata set here, and who releases it
    // (or is it even properly ref counted)? And who closes the stream
    // etc?
  } else {
    ns4xStreamWrapper* wrapper = (ns4xStreamWrapper *)pstream->ndata;
    NS_ASSERTION(wrapper != NULL, "null wrapper");

    if (wrapper == NULL)
      return NPERR_INVALID_PARAM;

    // This will release the wrapped nsIOutputStream.
    delete wrapper;
    pstream->ndata = nsnull;
  }

  return NPERR_NO_ERROR;
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_status(NPP npp, const char *message)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_status called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_Status: npp=%p, message=%s\n",
                                     (void*)npp, message));

  if (!npp || !npp->ndata) {
    NS_WARNING("_status: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstancePeer> peer;
  if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) && peer) {
    peer->ShowStatus(message);
  }
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_memfree (void *ptr)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_memfree called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemFree: ptr=%p\n", ptr));

  if (ptr)
    nsMemory::Free(ptr);
}


////////////////////////////////////////////////////////////////////////
uint32 NP_CALLBACK
_memflush(uint32 size)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_memflush called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemFlush: size=%d\n", size));

  nsMemory::HeapMinimize(PR_TRUE);
  return 0;
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_reloadplugins(NPBool reloadPages)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_reloadplugins called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_ReloadPlugins: reloadPages=%d\n", reloadPages));

  nsCOMPtr<nsIPluginManager> pm(do_GetService(kPluginManagerCID));
  if (!pm)
    return;

  pm->ReloadPlugins(reloadPages);
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_invalidaterect(NPP npp, NPRect *invalidRect)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invalidaterect called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_InvalidateRect: npp=%p, top=%d, left=%d, bottom=%d, "
                  "right=%d\n", (void *)npp, invalidRect->top,
                  invalidRect->left, invalidRect->bottom, invalidRect->right));

  if (!npp || !npp->ndata) {
    NS_WARNING("_invalidaterect: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstancePeer> peer;
  if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) && peer) {
    nsCOMPtr<nsIWindowlessPluginInstancePeer> wpeer(do_QueryInterface(peer));
    if (wpeer) {
      // XXX nsRect & NPRect are structurally equivalent
      wpeer->InvalidateRect((nsPluginRect *)invalidRect);
    }
  }
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_invalidateregion(NPP npp, NPRegion invalidRegion)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invalidateregion called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,
                 ("NPN_InvalidateRegion: npp=%p, region=%p\n", (void*)npp,
                  (void*)invalidRegion));

  if (!npp || !npp->ndata) {
    NS_WARNING("_invalidateregion: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstancePeer> peer;
  if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) && peer) {
    nsCOMPtr<nsIWindowlessPluginInstancePeer> wpeer(do_QueryInterface(peer));
    if (wpeer) {
      // nsPluginRegion & NPRegion are typedef'd to the same thing
      wpeer->InvalidateRegion((nsPluginRegion)invalidRegion);
    }
  }
}


////////////////////////////////////////////////////////////////////////
void NP_CALLBACK
_forceredraw(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_forceredraw called from the wrong thread\n"));
    return;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_ForceDraw: npp=%p\n", (void*)npp));

  if (!npp || !npp->ndata) {
    NS_WARNING("_forceredraw: npp or npp->ndata == 0");
    return;
  }

  nsIPluginInstance *inst = (nsIPluginInstance *) npp->ndata;

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstancePeer> peer;
  if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) && peer) {
    nsCOMPtr<nsIWindowlessPluginInstancePeer> wpeer(do_QueryInterface(peer));
    if (wpeer) {
      wpeer->ForceRedraw();
    }
  }
}

static nsIDocument *
GetDocumentFromNPP(NPP npp)
{
  NS_ENSURE_TRUE(npp, nsnull);

  ns4xPluginInstance *inst = (ns4xPluginInstance *)npp->ndata;
  NS_ENSURE_TRUE(inst, nsnull);

  PluginDestructionGuard guard(inst);

  nsCOMPtr<nsIPluginInstancePeer> pip;
  inst->GetPeer(getter_AddRefs(pip));
  nsCOMPtr<nsPIPluginInstancePeer> pp(do_QueryInterface(pip));
  NS_ENSURE_TRUE(pp, nsnull);

  nsCOMPtr<nsIPluginInstanceOwner> owner;
  pp->GetOwner(getter_AddRefs(owner));
  NS_ENSURE_TRUE(owner, nsnull);

  nsCOMPtr<nsIDocument> doc;
  owner->GetDocument(getter_AddRefs(doc));

  return doc;
}

static JSContext *
GetJSContextFromDoc(nsIDocument *doc)
{
  nsIScriptGlobalObject *sgo = doc->GetScriptGlobalObject();
  NS_ENSURE_TRUE(sgo, nsnull);

  nsIScriptContext *scx = sgo->GetContext();
  NS_ENSURE_TRUE(scx, nsnull);

  return (JSContext *)scx->GetNativeContext();
}

static JSContext *
GetJSContextFromNPP(NPP npp)
{
  nsIDocument *doc = GetDocumentFromNPP(npp);
  NS_ENSURE_TRUE(doc, nsnull);

  return GetJSContextFromDoc(doc);
}

NPObject* NP_CALLBACK
_getwindowobject(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getwindowobject called from the wrong thread\n"));
    return nsnull;
  }
  JSContext *cx = GetJSContextFromNPP(npp);
  NS_ENSURE_TRUE(cx, nsnull);

  // Using ::JS_GetGlobalObject(cx) is ok here since the window we
  // want to return here is the outer window, *not* the inner (since
  // we don't know what the plugin will do with it).
  return nsJSObjWrapper::GetNewOrUsed(npp, cx, ::JS_GetGlobalObject(cx));
}

NPObject* NP_CALLBACK
_getpluginelement(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getpluginelement called from the wrong thread\n"));
    return nsnull;
  }
  nsIDOMElement *elementp = nsnull;
  NPError nperr = _getvalue(npp, NPNVDOMElement, &elementp);

  if (nperr != NPERR_NO_ERROR) {
    return nsnull;
  }

  // Pass ownership of elementp to element
  nsCOMPtr<nsIDOMElement> element;
  element.swap(elementp);

  JSContext *cx = GetJSContextFromNPP(npp);
  NS_ENSURE_TRUE(cx, nsnull);

  nsCOMPtr<nsIXPConnect> xpc(do_GetService(nsIXPConnect::GetCID()));
  NS_ENSURE_TRUE(xpc, nsnull);

  nsCOMPtr<nsIXPConnectJSObjectHolder> holder;
  xpc->WrapNative(cx, ::JS_GetGlobalObject(cx), element,
                  NS_GET_IID(nsIDOMElement),
                  getter_AddRefs(holder));
  NS_ENSURE_TRUE(holder, nsnull);

  JSObject* obj = nsnull;
  holder->GetJSObject(&obj);
  NS_ENSURE_TRUE(obj, nsnull);

  return nsJSObjWrapper::GetNewOrUsed(npp, cx, obj);
}

static NPIdentifier
doGetIdentifier(JSContext *cx, const NPUTF8* name)
{
  NS_ConvertUTF8toUTF16 utf16name(name);

  JSString *str = ::JS_InternUCStringN(cx, (jschar *)utf16name.get(),
                                       utf16name.Length());

  if (!str)
    return NULL;

  return (NPIdentifier)STRING_TO_JSVAL(str);
}

NPIdentifier NP_CALLBACK
_getstringidentifier(const NPUTF8* name)
{
  if (!name) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS, ("NPN_getstringidentifier: passed null name"));
    return NULL;
  }
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifier called from the wrong thread\n"));
  }

  nsCOMPtr<nsIThreadJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  if (!stack)
    return NULL;

  JSContext *cx = nsnull;
  stack->GetSafeJSContext(&cx);
  if (!cx)
    return NULL;

  JSAutoRequest ar(cx);
  return doGetIdentifier(cx, name);
}

void NP_CALLBACK
_getstringidentifiers(const NPUTF8** names, int32_t nameCount,
                      NPIdentifier *identifiers)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifiers called from the wrong thread\n"));
  }
  nsCOMPtr<nsIThreadJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  if (!stack)
    return;

  JSContext *cx = nsnull;
  stack->GetSafeJSContext(&cx);
  if (!cx)
    return;

  JSAutoRequest ar(cx);

  for (int32_t i = 0; i < nameCount; ++i) {
    if (names[i]) {
      identifiers[i] = doGetIdentifier(cx, names[i]);
    } else {
      NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS, ("NPN_getstringidentifiers: passed null name"));
      identifiers[i] = NULL;
    }
  }
}

NPIdentifier NP_CALLBACK
_getintidentifier(int32_t intid)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getstringidentifier called from the wrong thread\n"));
  }
  return (NPIdentifier)INT_TO_JSVAL(intid);
}

NPUTF8* NP_CALLBACK
_utf8fromidentifier(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_utf8fromidentifier called from the wrong thread\n"));
  }
  if (!identifier)
    return NULL;

  jsval v = (jsval)identifier;

  if (!JSVAL_IS_STRING(v)) {
    return nsnull;
  }

  JSString *str = JSVAL_TO_STRING(v);

  return
    ToNewUTF8String(nsDependentString((PRUnichar *)::JS_GetStringChars(str),
                                      ::JS_GetStringLength(str)));
}

int32_t NP_CALLBACK
_intfromidentifier(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_intfromidentifier called from the wrong thread\n"));
  }
  jsval v = (jsval)identifier;

  if (!JSVAL_IS_INT(v)) {
    return PR_INT32_MIN;
  }

  return JSVAL_TO_INT(v);
}

bool NP_CALLBACK
_identifierisstring(NPIdentifier identifier)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_identifierisstring called from the wrong thread\n"));
  }
  jsval v = (jsval)identifier;

  return JSVAL_IS_STRING(v);
}

NPObject* NP_CALLBACK
_createobject(NPP npp, NPClass* aClass)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_createobject called from the wrong thread\n"));
    return nsnull;
  }
  if (!npp) {
    NS_ERROR("Null npp passed to _createobject()!");

    return nsnull;
  }

  PluginDestructionGuard guard(npp);

  if (!aClass) {
    NS_ERROR("Null class passed to _createobject()!");

    return nsnull;
  }

  NPPAutoPusher nppPusher(npp);

  NPObject *npobj;

  if (aClass->allocate) {
    npobj = aClass->allocate(npp, aClass);
  } else {
    npobj = (NPObject *)PR_Malloc(sizeof(NPObject));
  }

  if (npobj) {
    npobj->_class = aClass;
    npobj->referenceCount = 1;
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("Created NPObject %p, NPClass %p\n", npobj, aClass));

  return npobj;
}

NPObject* NP_CALLBACK
_retainobject(NPObject* npobj)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_retainobject called from the wrong thread\n"));
  }
  if (npobj) {
    PR_AtomicIncrement((PRInt32*)&npobj->referenceCount);
  }

  return npobj;
}

void NP_CALLBACK
_releaseobject(NPObject* npobj)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_releaseobject called from the wrong thread\n"));
  }
  if (!npobj)
    return;

  int32_t refCnt = PR_AtomicDecrement((PRInt32*)&npobj->referenceCount);

  if (refCnt == 0) {
    nsNPObjWrapper::OnDestroy(npobj);

    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                   ("Deleting NPObject %p, refcount hit 0\n", npobj));

    if (npobj->_class && npobj->_class->deallocate) {
      npobj->_class->deallocate(npobj);
    } else {
      PR_Free(npobj);
    }
  }
}

bool NP_CALLBACK
_invoke(NPP npp, NPObject* npobj, NPIdentifier method, const NPVariant *args,
        uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invoke called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->invoke)
    return false;

  PluginDestructionGuard guard(npp);

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Invoke(npp %p, npobj %p, method %p, args %d\n", npp,
                  npobj, method, argCount));

  return npobj->_class->invoke(npobj, method, args, argCount, result);
}

bool NP_CALLBACK
_invokeDefault(NPP npp, NPObject* npobj, const NPVariant *args,
               uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_invokedefault called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->invokeDefault)
    return false;

  // Hack to work around bug in the Sun Java Deployment Toolkit plugin
  // where it doesn't properly initialize variants when calling into
  // invokeDefault(). See bug 498132.
  ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;
  if (inst) {
    nsCOMPtr<nsIPluginInstancePeer> pip;
    inst->GetPeer(getter_AddRefs(pip));
    if (pip) {
      nsMIMEType mimetype = nsnull;
      pip->GetMIMEType(&mimetype);
      if (mimetype && strcmp(mimetype, "application/npruntime-scriptable-plugin;DeploymentToolkit") == 0) {
        VOID_TO_NPVARIANT(*result);
      }
    }
  }

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_InvokeDefault(npp %p, npobj %p, args %d\n", npp,
                  npobj, argCount));

  return npobj->_class->invokeDefault(npobj, args, argCount, result);
}

bool NP_CALLBACK
_evaluate(NPP npp, NPObject* npobj, NPString *script, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_evaluate called from the wrong thread\n"));
    return false;
  }
  if (!npp)
    return false;

  NPPAutoPusher nppPusher(npp);

  nsIDocument *doc = GetDocumentFromNPP(npp);
  NS_ENSURE_TRUE(doc, false);

  JSContext *cx = GetJSContextFromDoc(doc);
  NS_ENSURE_TRUE(cx, false);

  JSObject *obj =
    nsNPObjWrapper::GetNewOrUsed(npp, cx, npobj);

  if (!obj) {
    return false;
  }

  // Root obj and the rval (below).
  jsval vec[] = { OBJECT_TO_JSVAL(obj), JSVAL_NULL };
  JSAutoTempValueRooter tvr(cx, NS_ARRAY_LENGTH(vec), vec);
  jsval *rval = &vec[1];

  if (result) {
    // Initialize the out param to void
    VOID_TO_NPVARIANT(*result);
  }

  if (!script || !script->utf8length || !script->utf8characters) {
    // Nothing to evaluate.

    return true;
  }

  NS_ConvertUTF8toUTF16 utf16script(script->utf8characters,
                                    script->utf8length);

  nsCOMPtr<nsIScriptContext> scx = GetScriptContextFromJSContext(cx);
  NS_ENSURE_TRUE(scx, false);

  nsIPrincipal *principal = doc->NodePrincipal();

  nsCAutoString specStr;
  const char *spec;

  nsCOMPtr<nsIURI> uri;
  principal->GetURI(getter_AddRefs(uri));

  if (uri) {
    uri->GetSpec(specStr);
    spec = specStr.get();
  } else {
    // No URI in a principal means it's the system principal. If the
    // document URI is a chrome:// URI, pass that in as the URI of the
    // script, else pass in null for the filename as there's no way to
    // know where this document really came from. Passing in null here
    // also means that the script gets treated by XPConnect as if it
    // needs additional protection, which is what we want for unknown
    // chrome code anyways.

    uri = doc->GetDocumentURI();
    PRBool isChrome = PR_FALSE;

    if (uri && NS_SUCCEEDED(uri->SchemeIs("chrome", &isChrome)) && isChrome) {
      uri->GetSpec(specStr);
      spec = specStr.get();
    } else {
      spec = nsnull;
    }
  }

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Evaluate(npp %p, npobj %p, script <<<%s>>>) called\n",
                  npp, npobj, script->utf8characters));

  nsresult rv = scx->EvaluateStringWithValue(utf16script, obj, principal,
                                             spec, 0, 0, rval, nsnull);

  return NS_SUCCEEDED(rv) &&
         (!result || JSValToNPVariant(npp, cx, *rval, result));
}

bool NP_CALLBACK
_getproperty(NPP npp, NPObject* npobj, NPIdentifier property,
             NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->getProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_GetProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->getProperty(npobj, property, result);
}

bool NP_CALLBACK
_setproperty(NPP npp, NPObject* npobj, NPIdentifier property,
             const NPVariant *value)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->setProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_SetProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->setProperty(npobj, property, value);
}

bool NP_CALLBACK
_removeproperty(NPP npp, NPObject* npobj, NPIdentifier property)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_removeproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->removeProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_RemoveProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, property));

  return npobj->_class->removeProperty(npobj, property);
}

bool NP_CALLBACK
_hasproperty(NPP npp, NPObject* npobj, NPIdentifier propertyName)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_hasproperty called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->hasProperty)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_HasProperty(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, propertyName));

  return npobj->_class->hasProperty(npobj, propertyName);
}

bool NP_CALLBACK
_hasmethod(NPP npp, NPObject* npobj, NPIdentifier methodName)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_hasmethod called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class || !npobj->_class->hasMethod)
    return false;

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_HasMethod(npp %p, npobj %p, property %p) called\n",
                  npp, npobj, methodName));

  return npobj->_class->hasProperty(npobj, methodName);
}

bool NP_CALLBACK
_enumerate(NPP npp, NPObject *npobj, NPIdentifier **identifier,
           uint32_t *count)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_enumerate called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class)
    return false;

  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,
                 ("NPN_Enumerate(npp %p, npobj %p) called\n", npp, npobj));

  if (!NP_CLASS_STRUCT_VERSION_HAS_ENUM(npobj->_class) ||
      !npobj->_class->enumerate) {
    *identifier = 0;
    *count = 0;
    return true;
  }

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  return npobj->_class->enumerate(npobj, identifier, count);
}

bool NP_CALLBACK
_construct(NPP npp, NPObject* npobj, const NPVariant *args,
               uint32_t argCount, NPVariant *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_construct called from the wrong thread\n"));
    return false;
  }
  if (!npp || !npobj || !npobj->_class ||
      !NP_CLASS_STRUCT_VERSION_HAS_CTOR(npobj->_class) ||
      !npobj->_class->construct) {
    return false;
  }

  NPPExceptionAutoHolder nppExceptionHolder;
  NPPAutoPusher nppPusher(npp);

  return npobj->_class->construct(npobj, args, argCount, result);
}

#ifdef MOZ_MEMORY_WINDOWS
extern "C" size_t malloc_usable_size(const void *ptr);

BOOL
InHeap(HANDLE hHeap, LPVOID lpMem)
{
  BOOL success = FALSE;
  PROCESS_HEAP_ENTRY he;
  he.lpData = NULL;
  while (HeapWalk(hHeap, &he) != 0) {
    if (he.lpData == lpMem) {
      success = TRUE;
      break;
    }
  }
  HeapUnlock(hHeap);
  return success;
}
#endif

void NP_CALLBACK
_releasevariantvalue(NPVariant* variant)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_releasevariantvalue called from the wrong thread\n"));
  }
  switch (variant->type) {
  case NPVariantType_Void :
  case NPVariantType_Null :
  case NPVariantType_Bool :
  case NPVariantType_Int32 :
  case NPVariantType_Double :
    break;
  case NPVariantType_String :
    {
      const NPString *s = &NPVARIANT_TO_STRING(*variant);

      if (s->utf8characters) {
#ifdef MOZ_MEMORY_WINDOWS
        if (malloc_usable_size((void *)s->utf8characters) != 0) {
          PR_Free((void *)s->utf8characters);
        } else {
          void *p = (void *)s->utf8characters;
          DWORD nheaps = 0;
          nsAutoTArray<HANDLE, 50> heaps;
          nheaps = GetProcessHeaps(0, heaps.Elements());
          heaps.AppendElements(nheaps);
          GetProcessHeaps(nheaps, heaps.Elements());
          for (DWORD i = 0; i < nheaps; i++) {
            if (InHeap(heaps[i], p)) {
              HeapFree(heaps[i], 0, p);
              break;
            }
          }
        }
#else
        PR_Free((void *)s->utf8characters);
#endif
      }
      break;
    }
  case NPVariantType_Object:
    {
      NPObject *npobj = NPVARIANT_TO_OBJECT(*variant);

      if (npobj)
        _releaseobject(npobj);

      break;
    }
  default:
    NS_ERROR("Unknown NPVariant type!");
  }

  VOID_TO_NPVARIANT(*variant);
}

bool NP_CALLBACK
_tostring(NPObject* npobj, NPVariant *result)
{
  NS_ERROR("Write me!");

  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_tostring called from the wrong thread\n"));
    return false;
  }

  return false;
}

static char *gNPPException;

void NP_CALLBACK
_setexception(NPObject* npobj, const NPUTF8 *message)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setexception called from the wrong thread\n"));
    return;
  }

  if (gNPPException) {
    // If a plugin throws multiple exceptions, we'll only report the
    // last one for now.
    free(gNPPException);
  }

  gNPPException = strdup(message);
}

const char *
PeekException()
{
  return gNPPException;
}

void
PopException()
{
  NS_ASSERTION(gNPPException, "Uh, no NPP exception to pop!");

  if (gNPPException) {
    free(gNPPException);

    gNPPException = nsnull;
  }
}

NPPExceptionAutoHolder::NPPExceptionAutoHolder()
  : mOldException(gNPPException)
{
  gNPPException = nsnull;
}

NPPExceptionAutoHolder::~NPPExceptionAutoHolder()
{
  NS_ASSERTION(!gNPPException, "NPP exception not properly cleared!");

  gNPPException = mOldException;
}

////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_getvalue(NPP npp, NPNVariable variable, void *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_getvalue called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetValue: npp=%p, var=%d\n",
                                     (void*)npp, (int)variable));

  nsresult res;

  PluginDestructionGuard guard(npp);

  switch(variable) {
#if defined(XP_UNIX) && !defined(XP_MACOSX)
  case NPNVxDisplay : {
#ifdef MOZ_WIDGET_GTK2
    if (npp) {
      ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;
      NPBool rtv = PR_FALSE;
      inst->GetValue((nsPluginInstanceVariable)NPPVpluginNeedsXEmbed, &rtv);
      if (rtv) {
        (*(Display **)result) = GDK_DISPLAY();
        return NPERR_NO_ERROR;
      }
    }
    // adobe nppdf calls XtGetApplicationNameAndClass(display,
    // &instance, &class) we have to init Xt toolkit before get
    // XtDisplay just call gtk_xtbin_new(w,0) once
    static GtkWidget *gtkXtBinHolder = 0;
    if (!gtkXtBinHolder) {
      gtkXtBinHolder = gtk_xtbin_new(GDK_ROOT_PARENT(),0);
      // it crashes on destroy, let it leak
      // gtk_widget_destroy(gtkXtBinHolder);
    }
    (*(Display **)result) =  GTK_XTBIN(gtkXtBinHolder)->xtdisplay;
    return NPERR_NO_ERROR;
#endif
    return NPERR_GENERIC_ERROR;
  }

  case NPNVxtAppContext:
    return NPERR_GENERIC_ERROR;
#endif

#if defined(XP_WIN) || defined(XP_OS2) || defined(MOZ_WIDGET_GTK2)
  case NPNVnetscapeWindow: {
    if (!npp || !npp->ndata)
      return NPERR_INVALID_INSTANCE_ERROR;

    ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;

    nsCOMPtr<nsIPluginInstancePeer> peer;
    if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) &&
        peer &&
        NS_SUCCEEDED(peer->GetValue(nsPluginInstancePeerVariable_NetscapeWindow,
                                    result))) {
      return NPERR_NO_ERROR;
    }
    return NPERR_GENERIC_ERROR;
  }
#endif

  case NPNVjavascriptEnabledBool: {
    *(NPBool*)result = PR_FALSE;
    nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
    if (prefs) {
      PRBool js = PR_FALSE;;
      res = prefs->GetBoolPref("javascript.enabled", &js);
      if (NS_SUCCEEDED(res))
        *(NPBool*)result = js;
    }
    return NPERR_NO_ERROR;
  }

  case NPNVasdEnabledBool:
    *(NPBool*)result = FALSE;
    return NPERR_NO_ERROR;

  case NPNVisOfflineBool: {
    PRBool offline = PR_FALSE;
    nsCOMPtr<nsIIOService> ioservice =
      do_GetService(NS_IOSERVICE_CONTRACTID, &res);
    if (NS_SUCCEEDED(res))
      res = ioservice->GetOffline(&offline);
    if (NS_FAILED(res))
      return NPERR_GENERIC_ERROR;

    *(NPBool*)result = offline;
    return NPERR_NO_ERROR;
  }

  case NPNVserviceManager: {
    nsIServiceManager * sm;
    res = NS_GetServiceManager(&sm);
    if (NS_SUCCEEDED(res)) {
      *(nsIServiceManager**)result = sm;
      return NPERR_NO_ERROR;
    } else
      return NPERR_GENERIC_ERROR;
  }

  case NPNVDOMElement: {
    ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;
    NS_ENSURE_TRUE(inst, NPERR_GENERIC_ERROR);

    nsCOMPtr<nsIPluginInstancePeer> pip;
    inst->GetPeer(getter_AddRefs(pip));
    nsCOMPtr<nsIPluginTagInfo2> pti2 (do_QueryInterface(pip));
    if (pti2) {
      nsCOMPtr<nsIDOMElement> e;
      pti2->GetDOMElement(getter_AddRefs(e));
      if (e) {
        NS_ADDREF(*(nsIDOMElement**)result = e.get());
        return NPERR_NO_ERROR;
      }
    }
    return NPERR_GENERIC_ERROR;
  }

  case NPNVDOMWindow: {
    ns4xPluginInstance *inst = (ns4xPluginInstance *)npp->ndata;
    NS_ENSURE_TRUE(inst, NPERR_GENERIC_ERROR);

    nsIDOMWindow *domWindow = inst->GetDOMWindow().get();

    if (domWindow) {
      // Pass over ownership of domWindow to the caller.
      (*(nsIDOMWindow**)result) = domWindow;

      return NPERR_NO_ERROR;
    }
    return NPERR_GENERIC_ERROR;
  }

  case NPNVToolkit: {
#ifdef MOZ_WIDGET_GTK2
    *((NPNToolkitType*)result) = NPNVGtk2;
#endif

    if (*(NPNToolkitType*)result)
        return NPERR_NO_ERROR;

    return NPERR_GENERIC_ERROR;
  }

  case NPNVSupportsXEmbedBool: {
#ifdef MOZ_WIDGET_GTK2
    *(NPBool*)result = PR_TRUE;
#else
    *(NPBool*)result = PR_FALSE;
#endif
    return NPERR_NO_ERROR;
  }

  case NPNVWindowNPObject: {
    *(NPObject **)result = _getwindowobject(npp);

    return NPERR_NO_ERROR;
  }

  case NPNVPluginElementNPObject: {
    *(NPObject **)result = _getpluginelement(npp);

    return NPERR_NO_ERROR;
  }

  case NPNVSupportsWindowless: {
#if defined(XP_WIN) || defined(XP_MACOSX) || (defined(MOZ_X11) && defined(MOZ_WIDGET_GTK2))
    *(NPBool*)result = PR_TRUE;
#else
    *(NPBool*)result = PR_FALSE;
#endif
    return NPERR_NO_ERROR;
  }

#ifdef XP_MACOSX
  case NPNVpluginDrawingModel: {
    if (npp) {
      ns4xPluginInstance *inst = (ns4xPluginInstance*)npp->ndata;
      if (inst) {
        *(NPDrawingModel*)result = inst->GetDrawingModel();
        return NPERR_NO_ERROR;
      }
    }
    else {
      return NPERR_GENERIC_ERROR;
    }
  }

#ifndef NP_NO_QUICKDRAW
  case NPNVsupportsQuickDrawBool: {
    *(NPBool*)result = PR_TRUE;
    
    return NPERR_NO_ERROR;
  }
#endif

  case NPNVsupportsCoreGraphicsBool: {
    *(NPBool*)result = PR_TRUE;
    
    return NPERR_NO_ERROR;
  }
#endif

  default : return NPERR_GENERIC_ERROR;
  }
}


////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_setvalue(NPP npp, NPPVariable variable, void *result)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_setvalue called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_SetValue: npp=%p, var=%d\n",
                                     (void*)npp, (int)variable));

  if (!npp)
    return NPERR_INVALID_INSTANCE_ERROR;

  ns4xPluginInstance *inst = (ns4xPluginInstance *) npp->ndata;

  NS_ASSERTION(inst != NULL, "null instance");

  if (inst == NULL)
    return NPERR_INVALID_INSTANCE_ERROR;

  PluginDestructionGuard guard(inst);

  switch (variable) {

    // we should keep backward compatibility with 4x where the
    // actual pointer value is checked rather than its content
    // when passing booleans
    case NPPVpluginWindowBool: {
      NPBool bWindowless = (result == nsnull);
      return inst->SetWindowless(bWindowless);
    }

    case NPPVpluginTransparentBool: {
      NPBool bTransparent = (result != nsnull);
      return inst->SetTransparent(bTransparent);
    }

    case NPPVjavascriptPushCallerBool:
      {
        nsresult rv;
        nsCOMPtr<nsIJSContextStack> contextStack =
          do_GetService("@mozilla.org/js/xpc/ContextStack;1", &rv);
        if (NS_SUCCEEDED(rv)) {
          NPBool bPushCaller = (result != nsnull);

          if (bPushCaller) {
            rv = NS_ERROR_FAILURE;

            nsCOMPtr<nsIPluginInstancePeer> peer;
            if (NS_SUCCEEDED(inst->GetPeer(getter_AddRefs(peer))) && peer) {
              nsCOMPtr<nsIPluginInstancePeer2> peer2 =
                do_QueryInterface(peer);

              if (peer2) {
                JSContext *cx;
                rv = peer2->GetJSContext(&cx);

                if (NS_SUCCEEDED(rv))
                  rv = contextStack->Push(cx);
              }
            }
          } else {
            rv = contextStack->Pop(nsnull);
          }
        }
        return NS_SUCCEEDED(rv) ? NPERR_NO_ERROR : NPERR_GENERIC_ERROR;
      }
      break;

    case NPPVpluginKeepLibraryInMemory: {
      NPBool bCached = (result != nsnull);
      return inst->SetCached(bCached);
    }
      
#ifdef XP_MACOSX
    case NPPVpluginDrawingModel: {
      if (inst) {
        int dModelValue = (int)result;
        inst->SetDrawingModel((NPDrawingModel)dModelValue);
        return NPERR_NO_ERROR;
      }
      else {
        return NPERR_GENERIC_ERROR;
      }
    }
#endif

    default:
      return NPERR_NO_ERROR;
  }
}

////////////////////////////////////////////////////////////////////////
NPError NP_CALLBACK
_requestread(NPStream *pstream, NPByteRange *rangeList)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_requestread called from the wrong thread\n"));
    return NPERR_INVALID_PARAM;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_RequestRead: stream=%p\n",
                                     (void*)pstream));

#ifdef PLUGIN_LOGGING
  for(NPByteRange * range = rangeList; range != nsnull; range = range->next)
    PR_LOG(nsPluginLogging::gNPNLog,PLUGIN_LOG_NOISY,
    ("%i-%i", range->offset, range->offset + range->length - 1));

  PR_LOG(nsPluginLogging::gNPNLog,PLUGIN_LOG_NOISY, ("\n\n"));
  PR_LogFlush();
#endif

  if (!pstream || !rangeList || !pstream->ndata)
    return NPERR_INVALID_PARAM;

  ns4xPluginStreamListener * streamlistener =
    (ns4xPluginStreamListener *)pstream->ndata;

  nsPluginStreamType streamtype = nsPluginStreamType_Normal;

  streamlistener->GetStreamType(&streamtype);

  if (streamtype != nsPluginStreamType_Seek)
    return NPERR_STREAM_NOT_SEEKABLE;

  if (streamlistener->mStreamInfo)
    streamlistener->mStreamInfo->RequestRead((nsByteRange *)rangeList);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////
#ifdef OJI
JRIEnv* NP_CALLBACK
_getJavaEnv(void)
{
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetJavaEnv\n"));
  return NULL;
}
#endif

////////////////////////////////////////////////////////////////////////
const char * NP_CALLBACK
_useragent(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_useragent called from the wrong thread\n"));
    return nsnull;
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_UserAgent: npp=%p\n", (void*)npp));

  nsCOMPtr<nsIPluginManager> pm(do_GetService(kPluginManagerCID));
  if (!pm)
    return nsnull;

  const char *retstr;
  nsresult rv = pm->UserAgent(&retstr);
  if (NS_FAILED(rv))
    return nsnull;

  return retstr;
}


////////////////////////////////////////////////////////////////////////
void * NP_CALLBACK
_memalloc (uint32 size)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL,("NPN_memalloc called from the wrong thread\n"));
  }
  NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY, ("NPN_MemAlloc: size=%d\n", size));
  return nsMemory::Alloc(size);
}

#ifdef OJI
////////////////////////////////////////////////////////////////////////
jref NP_CALLBACK
_getJavaPeer(NPP npp)
{
  NPN_PLUGIN_LOG(PLUGIN_LOG_NORMAL, ("NPN_GetJavaPeer: npp=%p\n", (void*)npp));
  return NULL;
}

#endif /* OJI */

void NP_CALLBACK
_pushpopupsenabledstate(NPP npp, NPBool enabled)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_pushpopupsenabledstate called from the wrong thread\n"));
    return;
  }
  ns4xPluginInstance *inst = (ns4xPluginInstance *)npp->ndata;
  if (!inst)
    return;

  inst->PushPopupsEnabledState(enabled);
}

void NP_CALLBACK
_poppopupsenabledstate(NPP npp)
{
  if (!NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_ALWAYS,("NPN_poppopupsenabledstate called from the wrong thread\n"));
    return;
  }
  ns4xPluginInstance *inst = (ns4xPluginInstance *)npp->ndata;
  if (!inst)
    return;

  inst->PopPopupsEnabledState();
}

class nsPluginThreadRunnable : public nsRunnable,
                               public PRCList
{
public:
  nsPluginThreadRunnable(NPP instance, PluginThreadCallback func,
                         void *userData);
  virtual ~nsPluginThreadRunnable();

  NS_IMETHOD Run();

  PRBool IsForInstance(NPP instance)
  {
    return (mInstance == instance);
  }

  void Invalidate()
  {
    mFunc = nsnull;
  }

  PRBool IsValid()
  {
    return (mFunc != nsnull);
  }

private:  
  NPP mInstance;
  PluginThreadCallback mFunc;
  void *mUserData;
};

nsPluginThreadRunnable::nsPluginThreadRunnable(NPP instance,
                                               PluginThreadCallback func,
                                               void *userData)
  : mInstance(instance), mFunc(func), mUserData(userData)
{
  if (!sPluginThreadAsyncCallLock) {
    // Failed to create lock, not much we can do here then...
    mFunc = nsnull;

    return;
  }

  PR_INIT_CLIST(this);

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    ns4xPluginInstance *inst = (ns4xPluginInstance *)instance->ndata;
    if (!inst || !inst->IsStarted()) {
      // The plugin was stopped, ignore this async call.
      mFunc = nsnull;

      return;
    }

    PR_APPEND_LINK(this, &sPendingAsyncCalls);
  }
}

nsPluginThreadRunnable::~nsPluginThreadRunnable()
{
  if (!sPluginThreadAsyncCallLock) {
    return;
  }

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    PR_REMOVE_LINK(this);
  }
}

NS_IMETHODIMP
nsPluginThreadRunnable::Run()
{
  if (mFunc) {
    PluginDestructionGuard guard(mInstance);

    NS_TRY_SAFE_CALL_VOID(mFunc(mUserData), nsnull, nsnull);
  }

  return NS_OK;
}

void NP_CALLBACK
_pluginthreadasynccall(NPP instance, PluginThreadCallback func, void *userData)
{
  if (NS_IsMainThread()) {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,("NPN_pluginthreadasynccall called from the main thread\n"));
  } else {
    NPN_PLUGIN_LOG(PLUGIN_LOG_NOISY,("NPN_pluginthreadasynccall called from a non main thread\n"));
  }
  nsRefPtr<nsPluginThreadRunnable> evt =
    new nsPluginThreadRunnable(instance, func, userData);

  if (evt && evt->IsValid()) {
    NS_DispatchToMainThread(evt);
  }
}

void
OnPluginDestroy(NPP instance)
{
  if (!sPluginThreadAsyncCallLock) {
    return;
  }

  {
    nsAutoLock lock(sPluginThreadAsyncCallLock);

    if (PR_CLIST_IS_EMPTY(&sPendingAsyncCalls)) {
      return;
    }

    nsPluginThreadRunnable *r =
      (nsPluginThreadRunnable *)PR_LIST_HEAD(&sPendingAsyncCalls);

    do {
      if (r->IsForInstance(instance)) {
        r->Invalidate();
      }

      r = (nsPluginThreadRunnable *)PR_NEXT_LINK(r);
    } while (r != &sPendingAsyncCalls);
  }
}

void
OnShutdown()
{
  NS_ASSERTION(PR_CLIST_IS_EMPTY(&sPendingAsyncCalls),
               "Pending async plugin call list not cleaned up!");

  if (sPluginThreadAsyncCallLock) {
    nsAutoLock::DestroyLock(sPluginThreadAsyncCallLock);

    sPluginThreadAsyncCallLock = nsnull;
  }
}

void
EnterAsyncPluginThreadCallLock()
{
  if (sPluginThreadAsyncCallLock) {
    PR_Lock(sPluginThreadAsyncCallLock);
  }
}

void
ExitAsyncPluginThreadCallLock()
{
  if (sPluginThreadAsyncCallLock) {
    PR_Unlock(sPluginThreadAsyncCallLock);
  }
}

NPP NPPStack::sCurrentNPP = nsnull;
