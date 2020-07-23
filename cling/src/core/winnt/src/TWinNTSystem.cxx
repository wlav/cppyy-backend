// @(#)root/winnt:$Id: db9b3139b1551a1b4e31a17f57866a276d5cd419 $
// Author: Fons Rademakers   15/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////////////
//                                                                              //
// TWinNTSystem                                                                 //
//                                                                              //
// Class providing an interface to the Windows NT/Windows 95 Operating Systems. //
//                                                                              //
//////////////////////////////////////////////////////////////////////////////////


#ifdef HAVE_CONFIG
#include "config.h"
#endif

#include "Windows4Root.h"
#include "ROOT/FoundationUtils.hxx"
#include "TWinNTSystem.h"
#include "TROOT.h"
#include "TError.h"
#include "TRegexp.h"
#include "TException.h"
#include "TEnv.h"
#include "TApplication.h"
#include "TInterpreter.h"
#include "TObjString.h"
#include "TUrl.h"

#include <sys/utime.h>
#include <sys/timeb.h>
#include <process.h>
#include <io.h>
#include <direct.h>
#include <ctype.h>
#include <float.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <lm.h>
#include <dbghelp.h>
#include <Tlhelp32.h>
#include <sstream>
#include <iostream>
#include <list>
#include <shlobj.h>
#include <conio.h>
#define Printf TStringPrintf
#if defined (_MSC_VER) && (_MSC_VER >= 1400)
   #include <intrin.h>
#elif defined (_M_IX86)
   static void __cpuid(int* cpuid_data, int info_type)
   {
      __asm {
         push ebx
         push edi
         mov edi, cpuid_data
         mov eax, info_type
         cpuid
         mov [edi], eax
         mov [edi + 4], ebx
         mov [edi + 8], ecx
         mov [edi + 12], edx
         pop edi
         pop ebx
      }
   }
   __int64 __rdtsc()
   {
      LARGE_INTEGER li;
      __asm {
         rdtsc
         mov li.LowPart, eax
         mov li.HighPart, edx
      }
      return li.QuadPart;
   }
#else
   static void __cpuid(int* cpuid_data, int) {
      cpuid_data[0] = 0x00000000;
      cpuid_data[1] = 0x00000000;
      cpuid_data[2] = 0x00000000;
      cpuid_data[3] = 0x00000000;
   }
   __int64 __rdtsc() { return (__int64)0; }
#endif

extern "C" {
   extern void Gl_setwidth(int width);
   void *_ReturnAddress(void);
}

namespace CppyyLegacy {

//////////////////// Windows TFdSet ////////////////////////////////////////////////
class TFdSet {
private:
   fd_set *fds_bits; // file descriptors (according MSDN maximum is 64)
public:
   TFdSet() { fds_bits = new fd_set; fds_bits->fd_count = 0; }
   virtual ~TFdSet() { delete fds_bits; }
   void  Copy(TFdSet &fd) const { memcpy((void*)fd.fds_bits, fds_bits, sizeof(fd_set)); }
   TFdSet(const TFdSet& fd) { fd.Copy(*this); }
   TFdSet& operator=(const TFdSet& fd)  { fd.Copy(*this); return *this; }
   void  Zero() { fds_bits->fd_count = 0; }
   void  Set(Int_t fd)
   {
      if (fds_bits->fd_count < FD_SETSIZE-1) // protect out of bound access (64)
         fds_bits->fd_array[fds_bits->fd_count++] = (SOCKET)fd;
      else
         ::CppyyLegacy::SysError("TFdSet::Set", "fd_count will exeed FD_SETSIZE");
   }
   void  Clr(Int_t fd)
   {
      int i;
      for (i=0; i<fds_bits->fd_count; i++) {
         if (fds_bits->fd_array[i]==(SOCKET)fd) {
            while (i<fds_bits->fd_count-1) {
               fds_bits->fd_array[i] = fds_bits->fd_array[i+1];
               i++;
            }
            fds_bits->fd_count--;
            break;
         }
      }
   }
   Int_t IsSet(Int_t fd) { return __WSAFDIsSet((SOCKET)fd, fds_bits); }
   Int_t *GetBits() { return fds_bits && fds_bits->fd_count ? (Int_t*)fds_bits : 0; }
   UInt_t GetCount() { return (UInt_t)fds_bits->fd_count; }
   Int_t GetFd(Int_t i) { return i<fds_bits->fd_count ? fds_bits->fd_array[i] : 0; }
};

namespace {
   const char *kProtocolName   = "tcp";
   typedef void (*SigHandler_t)(ESignals);

   static HANDLE gGlobalEvent;
   typedef NET_API_STATUS (WINAPI *pfn1)(LPVOID);
   typedef NET_API_STATUS (WINAPI *pfn2)(LPCWSTR, LPCWSTR, DWORD, LPBYTE*);
   typedef NET_API_STATUS (WINAPI *pfn3)(LPCWSTR, LPCWSTR, DWORD, LPBYTE*,
                                       DWORD, LPDWORD, LPDWORD, PDWORD);
   typedef NET_API_STATUS (WINAPI *pfn4)(LPCWSTR, DWORD, LPBYTE*, DWORD, LPDWORD,
                                       LPDWORD, PDWORD);
   static pfn1 p2NetApiBufferFree;
   static pfn2 p2NetUserGetInfo;
   static pfn3 p2NetLocalGroupGetMembers;
   static pfn4 p2NetLocalGroupEnum;

   static struct signal_map {
      int code;
      SigHandler_t handler;
      const char *signame;
   } signal_map[kMAXSIGNALS] = {
      -1 /*SIGBUS*/,   0, "bus error", 
      SIGSEGV,  0, "segmentation violation",
      -1 /*SIGSYS*/,   0, "bad argument to system call",
      -1 /*SIGPIPE*/,  0, "write on a pipe with no one to read it",
      SIGILL,   0, "illegal instruction",
      SIGABRT,  0, "abort",
      -1 /*SIGQUIT*/,  0, "quit",
      SIGINT,   0, "interrupt",
      -1 /*SIGWINCH*/, 0, "window size change",
      -1 /*SIGALRM*/,  0, "alarm clock",
      -1 /*SIGCHLD*/,  0, "death of a child",
      -1 /*SIGURG*/,   0, "urgent data arrived on an I/O channel",
      SIGFPE,   0, "floating point exception",
      SIGTERM,  0, "termination signal",
      -1 /*SIGUSR1*/,  0, "user-defined signal 1",
      -1 /*SIGUSR2*/,  0, "user-defined signal 2"
   };

   ////// static functions providing interface to raw WinNT ////////////////////

   /////////////////////////////////////////////////////////////////////////////
   /// Get shared library search path.

   static const char *DynamicPath(const char *newpath = 0, Bool_t reset = kFALSE)
   {
      static TString dynpath;

      if (reset || newpath) {
         dynpath = "";
      }
      if (newpath) {

         dynpath = newpath;

      } else if (dynpath == "") {
         TString rdynpath = gEnv ? gEnv->GetValue("Root.DynamicPath", (char*)0) : "";
         rdynpath.ReplaceAll("; ", ";");  // in case DynamicPath was extended
         if (rdynpath == "") {
            rdynpath = ".;"; rdynpath += TROOT::GetBinDir();
         }
         TString path = gSystem->Getenv("PATH");
         if (path == "")
            dynpath = rdynpath;
         else {
            dynpath = path; dynpath += ";"; dynpath += rdynpath;
         }

      }

      if (!dynpath.Contains(TROOT::GetLibDir())) {
         dynpath += ";"; dynpath += TROOT::GetLibDir();
      }

      return dynpath;
   }

   /////////////////////////////////////////////////////////////////////////////
   /// Call the signal handler associated with the signal.

   static void sighandler(int sig)
   {
      for (int i = 0; i < kMAXSIGNALS; i++) {
         if (signal_map[i].code == sig) {
            (*signal_map[i].handler)((ESignals)i);
            return;
         }
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   /// Set a signal handler for a signal.

   static void WinNTSignal(ESignals sig, SigHandler_t handler)
   {
      signal_map[sig].handler = handler;
      if (signal_map[sig].code != -1)
         (SigHandler_t)signal(signal_map[sig].code, sighandler);
   }

   /////////////////////////////////////////////////////////////////////////////
   /// Return the signal name associated with a signal.

   static const char *WinNTSigname(ESignals sig)
   {
      return signal_map[sig].signame;
   }

   /////////////////////////////////////////////////////////////////////////////
   /// WinNT signal handler.

   static BOOL ConsoleSigHandler(DWORD sig)
   {
      switch (sig) {
         case CTRL_C_EVENT:
            if (gSystem) {
               ((TWinNTSystem*)gSystem)->DispatchSignals(kSigInterrupt);
            }
            else {
               Break("TInterruptHandler::Notify", "keyboard interrupt");
               if (TROOT::Initialized()) {
                  gInterpreter->RewindDictionary();
               }
            }
            return kTRUE;
         case CTRL_BREAK_EVENT:
         case CTRL_LOGOFF_EVENT:
         case CTRL_SHUTDOWN_EVENT:
         case CTRL_CLOSE_EVENT:
         default:
            printf("\n *** Break *** keyboard interrupt - ROOT is terminated\n");
            gSystem->Exit(-1);
            return kTRUE;
      }
   }

   static CONTEXT *fgXcptContext = 0;
   /////////////////////////////////////////////////////////////////////////////

   static void SigHandler(ESignals sig)
   {
      if (gSystem)
         ((TWinNTSystem*)gSystem)->DispatchSignals(sig);
   }

   /////////////////////////////////////////////////////////////////////////////
   /// Function that's called when an unhandled exception occurs.
   /// Produces a stack trace, and lets the system deal with it
   /// as if it was an unhandled excecption (usually ::abort)

   LONG WINAPI ExceptionFilter(LPEXCEPTION_POINTERS pXcp)
   {
      fgXcptContext = pXcp->ContextRecord;
      gSystem->StackTrace();
      return EXCEPTION_CONTINUE_SEARCH;
   }


#pragma intrinsic(_ReturnAddress)
#pragma auto_inline(off)
   DWORD_PTR GetProgramCounter()
   {
      // Returns the current program counter.
      return (DWORD_PTR)_ReturnAddress();
   }
#pragma auto_inline(on)

   //=========================================================================
   // Load IMAGEHLP.DLL and get the address of functions in it that we'll use
   // by Microsoft, from http://www.microsoft.com/msj/0597/hoodtextfigs.htm#fig1
   //=========================================================================
   // Make typedefs for some IMAGEHLP.DLL functions so that we can use them
   // with GetProcAddress
   typedef BOOL (__stdcall *SYMINITIALIZEPROC)( HANDLE, LPSTR, BOOL );
   typedef BOOL (__stdcall *SYMCLEANUPPROC)( HANDLE );
   typedef BOOL (__stdcall *STACKWALK64PROC)
               ( DWORD, HANDLE, HANDLE, LPSTACKFRAME64, LPVOID,
               PREAD_PROCESS_MEMORY_ROUTINE,PFUNCTION_TABLE_ACCESS_ROUTINE,
               PGET_MODULE_BASE_ROUTINE, PTRANSLATE_ADDRESS_ROUTINE );
   typedef LPVOID (__stdcall *SYMFUNCTIONTABLEACCESS64PROC)( HANDLE, DWORD64 );
   typedef DWORD (__stdcall *SYMGETMODULEBASE64PROC)( HANDLE, DWORD64 );
   typedef BOOL (__stdcall *SYMGETMODULEINFO64PROC)(HANDLE, DWORD64, PIMAGEHLP_MODULE64);
   typedef BOOL (__stdcall *SYMGETSYMFROMADDR64PROC)( HANDLE, DWORD64, PDWORD64, PIMAGEHLP_SYMBOL64);
   typedef BOOL (__stdcall *SYMGETLINEFROMADDR64PROC)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
   typedef DWORD (__stdcall *UNDECORATESYMBOLNAMEPROC)(PCSTR, PSTR, DWORD, DWORD);


   static SYMINITIALIZEPROC _SymInitialize = 0;
   static SYMCLEANUPPROC _SymCleanup = 0;
   static STACKWALK64PROC _StackWalk64 = 0;
   static SYMFUNCTIONTABLEACCESS64PROC _SymFunctionTableAccess64 = 0;
   static SYMGETMODULEBASE64PROC _SymGetModuleBase64 = 0;
   static SYMGETMODULEINFO64PROC _SymGetModuleInfo64 = 0;
   static SYMGETSYMFROMADDR64PROC _SymGetSymFromAddr64 = 0;
   static SYMGETLINEFROMADDR64PROC _SymGetLineFromAddr64 = 0;
   static UNDECORATESYMBOLNAMEPROC _UnDecorateSymbolName = 0;

   BOOL InitImagehlpFunctions()
   {
      // Fetches function addresses from IMAGEHLP.DLL at run-time, so we
      // don't need to link against its import library. These functions
      // are used in StackTrace; if they cannot be found (e.g. because
      // IMAGEHLP.DLL doesn't exist or has the wrong version) we cannot
      // produce a stack trace.

      HMODULE hModImagehlp = LoadLibrary( "IMAGEHLP.DLL" );
      if (!hModImagehlp)
         return FALSE;

      _SymInitialize = (SYMINITIALIZEPROC) GetProcAddress( hModImagehlp, "SymInitialize" );
      if (!_SymInitialize)
         return FALSE;

      _SymCleanup = (SYMCLEANUPPROC) GetProcAddress( hModImagehlp, "SymCleanup" );
      if (!_SymCleanup)
         return FALSE;

      _StackWalk64 = (STACKWALK64PROC) GetProcAddress( hModImagehlp, "StackWalk64" );
      if (!_StackWalk64)
         return FALSE;

      _SymFunctionTableAccess64 = (SYMFUNCTIONTABLEACCESS64PROC) GetProcAddress(hModImagehlp, "SymFunctionTableAccess64" );
      if (!_SymFunctionTableAccess64)
         return FALSE;

      _SymGetModuleBase64=(SYMGETMODULEBASE64PROC)GetProcAddress(hModImagehlp, "SymGetModuleBase64");
      if (!_SymGetModuleBase64)
         return FALSE;

      _SymGetModuleInfo64=(SYMGETMODULEINFO64PROC)GetProcAddress(hModImagehlp, "SymGetModuleInfo64");
      if (!_SymGetModuleInfo64)
         return FALSE;

      _SymGetSymFromAddr64=(SYMGETSYMFROMADDR64PROC)GetProcAddress(hModImagehlp, "SymGetSymFromAddr64");
      if (!_SymGetSymFromAddr64)
         return FALSE;

      _SymGetLineFromAddr64=(SYMGETLINEFROMADDR64PROC)GetProcAddress(hModImagehlp, "SymGetLineFromAddr64");
      if (!_SymGetLineFromAddr64)
         return FALSE;

      _UnDecorateSymbolName=(UNDECORATESYMBOLNAMEPROC)GetProcAddress(hModImagehlp, "UnDecorateSymbolName");
      if (!_UnDecorateSymbolName)
         return FALSE;

      if (!_SymInitialize(GetCurrentProcess(), 0, TRUE ))
         return FALSE;

      return TRUE;
   }

   // stack trace helpers getModuleName, getFunctionName by
   /**************************************************************************
   * VRS - The Virtual Rendering System
   * Copyright (C) 2000-2004 Computer Graphics Systems Group at the
   * Hasso-Plattner-Institute (HPI), Potsdam, Germany.
   * This library is free software; you can redistribute it and/or modify it
   * under the terms of the GNU Lesser General Public License as published by
   * the Free Software Foundation; either version 2.1 of the License, or
   * (at your option) any later version.
   ***************************************************************************/
   std::string GetModuleName(DWORD64 address)
   {
      // Return the name of the module that contains the function at address.
      // Used by StackTrace.
      std::ostringstream out;
      HANDLE process = ::GetCurrentProcess();

      DWORD lineDisplacement = 0;
      IMAGEHLP_LINE64 line;
      ::ZeroMemory(&line, sizeof(line));
      line.SizeOfStruct = sizeof(line);
      if(_SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
            out << line.FileName << "(" << line.LineNumber << "): ";
      } else {
            IMAGEHLP_MODULE64 module;
            ::ZeroMemory(&module, sizeof(module));
            module.SizeOfStruct = sizeof(module);
            if(_SymGetModuleInfo64(process, address, &module)) {
               out << module.ModuleName << "!";
            } else {
               out << "0x" << std::hex << address << std::dec << " ";
            }
      }

      return out.str();
   }

   std::string GetFunctionName(DWORD64 address)
   {
      // Return the name of the function at address.
      // Used by StackTrace.
      DWORD64 symbolDisplacement = 0;
      HANDLE process = ::GetCurrentProcess();

      const unsigned int SYMBOL_BUFFER_SIZE = 8192;
      char symbolBuffer[SYMBOL_BUFFER_SIZE];
      PIMAGEHLP_SYMBOL64 symbol = reinterpret_cast<PIMAGEHLP_SYMBOL64>(symbolBuffer);
      ::ZeroMemory(symbol, SYMBOL_BUFFER_SIZE);
      symbol->SizeOfStruct = SYMBOL_BUFFER_SIZE;
      symbol->MaxNameLength = SYMBOL_BUFFER_SIZE - sizeof(IMAGEHLP_SYMBOL64);

      if(_SymGetSymFromAddr64(process, address, &symbolDisplacement, symbol)) {
            // Make the symbol readable for humans
            const unsigned int NAME_SIZE = 8192;
            char name[NAME_SIZE];
            _UnDecorateSymbolName(
               symbol->Name,
               name,
               NAME_SIZE,
               UNDNAME_COMPLETE             |
               UNDNAME_NO_THISTYPE          |
               UNDNAME_NO_SPECIAL_SYMS      |
               UNDNAME_NO_MEMBER_TYPE       |
               UNDNAME_NO_MS_KEYWORDS       |
               UNDNAME_NO_ACCESS_SPECIFIERS
            );

            std::string result;
            result += name;
            result += "()";
            return result;
      } else {
            return "??";
      }
   }

   ////// Shortcuts helper functions IsShortcut and ResolveShortCut ///////////

   /////////////////////////////////////////////////////////////////////////////
   /// Validates if a file name has extension '.lnk'. Returns true if file
   /// name have extension same as Window's shortcut file (.lnk).

   static BOOL IsShortcut(const char *filename)
   {
      //File extension for the Window's shortcuts (.lnk)
      const char *extLnk = ".lnk";
      if (filename != NULL) {
         //Validate extension
         TString strfilename(filename);
         if (strfilename.EndsWith(extLnk))
            return TRUE;
      }
      return FALSE;
   }

   /////////////////////////////////////////////////////////////////////////////
   /// Resolve a ShellLink (i.e. c:\path\shortcut.lnk) to a real path.

   static BOOL ResolveShortCut(LPCSTR pszShortcutFile, char *pszPath, int maxbuf)
   {
      HRESULT hres;
      IShellLink* psl;
      char szGotPath[MAX_PATH];
      WIN32_FIND_DATA wfd;

      *pszPath = 0;   // assume failure

      // Make typedefs for some ole32.dll functions so that we can use them
      // with GetProcAddress
      typedef HRESULT (__stdcall *COINITIALIZEPROC)( LPVOID );
      static COINITIALIZEPROC _CoInitialize = 0;
      typedef void (__stdcall *COUNINITIALIZEPROC)( void );
      static COUNINITIALIZEPROC _CoUninitialize = 0;
      typedef HRESULT (__stdcall *COCREATEINSTANCEPROC)( REFCLSID, LPUNKNOWN,
                       DWORD, REFIID, LPVOID );
      static COCREATEINSTANCEPROC _CoCreateInstance = 0;

      HMODULE hModImagehlp = LoadLibrary( "ole32.dll" );
      if (!hModImagehlp)
         return FALSE;

      _CoInitialize = (COINITIALIZEPROC) GetProcAddress( hModImagehlp, "CoInitialize" );
      if (!_CoInitialize)
         return FALSE;
      _CoUninitialize = (COUNINITIALIZEPROC) GetProcAddress( hModImagehlp, "CoUninitialize");
      if (!_CoUninitialize)
         return FALSE;
      _CoCreateInstance = (COCREATEINSTANCEPROC) GetProcAddress( hModImagehlp, "CoCreateInstance" );
      if (!_CoCreateInstance)
         return FALSE;

      _CoInitialize(NULL);

      hres = _CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                               IID_IShellLink, (void **) &psl);
      if (SUCCEEDED(hres)) {
         IPersistFile* ppf;

         hres = psl->QueryInterface(IID_IPersistFile, (void **) &ppf);
         if (SUCCEEDED(hres)) {
            WCHAR wsz[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, pszShortcutFile, -1, wsz, MAX_PATH);

            hres = ppf->Load(wsz, STGM_READ);
            if (SUCCEEDED(hres)) {
               hres = psl->Resolve(HWND_DESKTOP, SLR_ANY_MATCH | SLR_NO_UI | SLR_UPDATE);
               if (SUCCEEDED(hres)) {
                  strlcpy(szGotPath, pszShortcutFile,MAX_PATH);
                  hres = psl->GetPath(szGotPath, MAX_PATH, (WIN32_FIND_DATA *)&wfd,
                                      SLGP_UNCPRIORITY | SLGP_RAWPATH);
                  strlcpy(pszPath,szGotPath, maxbuf);
                  if (maxbuf) pszPath[maxbuf-1] = 0;
               }
            }
            ppf->Release();
         }
         psl->Release();
      }
      _CoUninitialize();

      return SUCCEEDED(hres);
   }

} // end unnamed namespace

} // namespace CppyyLegacy

///////////////////////////////////////////////////////////////////////////////
ClassImp(CppyyLegacy::TWinNTSystem);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////
/// ctor

TWinNTSystem::TWinNTSystem() : TSystem("WinNT", "WinNT System")
{
   fhProcess = ::GetCurrentProcess();
   fDirNameBuffer = 0;

   WSADATA WSAData;
   int initwinsock = 0;

   if (initwinsock = ::WSAStartup(MAKEWORD(2, 0), &WSAData)) {
      Error("TWinNTSystem()","Starting sockets failed");
   }

   // use ::MessageBeep by default for TWinNTSystem
   fBeepDuration = 1;
   fBeepFreq     = 0;
   if (gEnv) {
      fBeepDuration = gEnv->GetValue("Root.System.BeepDuration", 1);
      fBeepFreq     = gEnv->GetValue("Root.System.BeepFreq", 0);
   }

   char *buf = new char[MAX_MODULE_NAME32 + 1];

#ifdef ROOTPREFIX
   if (gSystem->Getenv("ROOTIGNOREPREFIX")) {
#endif
   // set ROOTSYS
   HMODULE hModCore = ::GetModuleHandle("libCoreLegacy.dll");
   if (hModCore) {
      ::GetModuleFileName(hModCore, buf, MAX_MODULE_NAME32 + 1);
      char *pLibName = strstr(buf, "libCoreLegacy.dll");
      if (pLibName) {
         --pLibName; // skip trailing \\ or /
         while (--pLibName >= buf && *pLibName != '\\' && *pLibName != '/');
         *pLibName = 0; // replace trailing \\ or / with 0
         TString check_path = buf;
         check_path += "\\etc";
         // look for $ROOTSYS (it should contain the "etc" subdirectory)
         while (buf[0] && GetFileAttributes(check_path.Data()) == INVALID_FILE_ATTRIBUTES) {
            while (--pLibName >= buf && *pLibName != '\\' && *pLibName != '/');
            *pLibName = 0;
            check_path = buf;
            check_path += "\\etc";
         }
         if (buf[0]) {
            Setenv("ROOTSYS", buf);
            TString path = buf;
            path += "\\bin;";
            path += Getenv("PATH");
            Setenv("PATH", path.Data());
         }
      }
   }
#ifdef ROOTPREFIX
   }
#endif

   delete [] buf;
}

////////////////////////////////////////////////////////////////////////////////
/// dtor

TWinNTSystem::~TWinNTSystem()
{
   // Clean up the WinSocket connectios
   ::WSACleanup();

   if (fDirNameBuffer) {
      delete [] fDirNameBuffer;
      fDirNameBuffer = 0;
   }

   if (gGlobalEvent) {
      ::ResetEvent(gGlobalEvent);
      ::CloseHandle(gGlobalEvent);
      gGlobalEvent = 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize WinNT system interface.

Bool_t TWinNTSystem::Init()
{
   const char *dir = 0;

   if (TSystem::Init()) {
      return kTRUE;
   }

   fReadmask = new TFdSet;
   fWritemask = new TFdSet;
   fReadready = new TFdSet;
   fWriteready = new TFdSet;
   fSignals = new TFdSet;
   fNfd    = 0;

   //--- install default handlers
   // Actually: don't. If we want a stack trace we need a context for the
   // signal. Signals don't have one. If we don't handle them, Windows will
   // raise an exception, which has a context, and which is handled by
   // ExceptionFilter.
   //WinNTSignal(kSigChild,                 SigHandler);
   //WinNTSignal(kSigBus,                   SigHandler);
   WinNTSignal(kSigSegmentationViolation, SigHandler);
   WinNTSignal(kSigIllegalInstruction,    SigHandler);
   WinNTSignal(kSigAbort,                 SigHandler);
   //WinNTSignal(kSigSystem,                SigHandler);
   //WinNTSignal(kSigPipe,                  SigHandler);
   //WinNTSignal(kSigAlarm,                 SigHandler);
   WinNTSignal(kSigFloatingException,     SigHandler);
   ::SetUnhandledExceptionFilter(ExceptionFilter);

   fSigcnt = 0;

   // This is a fallback in case TROOT::GetRootSys() can't determine ROOTSYS
   gRootDir = CppyyLegacy::FoundationUtils::GetFallbackRootSys().c_str();

   gGlobalEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);

   char *buf = new char[MAX_MODULE_NAME32 + 1];
   HMODULE hModCore = ::GetModuleHandle("libCoreLegacy.dll");
   if (hModCore) {
      ::GetModuleFileName(hModCore, buf, MAX_MODULE_NAME32 + 1);
      char *pLibName = strstr(buf, "libCoreLegacy.dll");
      --pLibName; // remove trailing \\ or /
      *pLibName = 0;
      // add the directory containing libCoreLegacy.dll in the dynamic search path
      if (buf[0]) AddDynamicPath(buf);
   }
   delete [] buf;
   fGroupsInitDone = kFALSE;
   fFirstFile = kTRUE;

   return kFALSE;
}

//---- Misc --------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Base name of a file name. Base name of /user/root is root.
/// But the base name of '/' is '/'
///                      'c:\' is 'c:\'

const char *TWinNTSystem::BaseName(const char *name)
{
   // BB 28/10/05 : Removed (commented out) StrDup() :
   // - To get same behaviour on Windows and on Linux
   // - To avoid the need to use #ifdefs
   // - Solve memory leaks (mainly in TTF::SetTextFont())
   // No need for the calling routine to use free() anymore.

   if (name) {
      int idx = 0;
      const char *symbol=name;

      // Skip leading blanks
      while ( (*symbol == ' ' || *symbol == '\t') && *symbol) symbol++;

      if (*symbol) {
         if (isalpha(symbol[idx]) && symbol[idx+1] == ':') idx = 2;
         if ( (symbol[idx] == '/'  ||  symbol[idx] == '\\')  &&  symbol[idx+1] == '\0') {
            //return StrDup(symbol);
            return symbol;
         }
      } else {
         Error("BaseName", "name = 0");
         return 0;
      }
      char *cp;
      char *bslash = (char *)strrchr(&symbol[idx],'\\');
      char *rslash = (char *)strrchr(&symbol[idx],'/');
      if (cp = (std::max)(rslash, bslash)) {
         //return StrDup(++cp);
         return ++cp;
      }
      //return StrDup(&symbol[idx]);
      return &symbol[idx];
   }
   Error("BaseName", "name = 0");
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Set the application name (from command line, argv[0]) and copy it in
/// gProgName. Copy the application pathname in gProgPath.

void TWinNTSystem::SetProgname(const char *name)
{
   ULong_t  idot = 0;
   char *dot = 0;
   char *progname;
   char *fullname = 0; // the program name with extension

  // On command prompt the progname can be supplied with no extension (under Windows)
   ULong_t namelen=name ? strlen(name) : 0;
   if (name && namelen > 0) {
      // Check whether the name contains "extention"
      fullname = new char[namelen+5];
      strlcpy(fullname, name,namelen+5);
      if ( !strrchr(fullname, '.') )
         strlcat(fullname, ".exe",namelen+5);

      progname = StrDup(BaseName(fullname));
      dot = strrchr(progname, '.');
      idot = dot ? (ULong_t)(dot - progname) : strlen(progname);

      char *which = 0;

      if (IsAbsoluteFileName(fullname) && !AccessPathName(fullname)) {
         which = StrDup(fullname);
      } else {
         which = Which(Form("%s;%s", WorkingDirectory(), Getenv("PATH")), progname);
      }

      if (which) {
         TString dirname;
         char driveletter = DriveName(which);
         const char *d = DirName(which);

         if (driveletter) {
            dirname.Form("%c:%s", driveletter, d);
         } else {
            dirname.Form("%s", d);
         }

         gProgPath = StrDup(dirname);
      } else {
         // Do not issue a warning - ROOT is not using gProgPath anyway.
         // Warning("SetProgname",
         //   "Cannot find this program named \"%s\" (Did you create a TApplication? Is this program in your %%PATH%%?)",
         //   fullname);
         gProgPath = WorkingDirectory();
      }

      // Cut the extension for progname off
      progname[idot] = '\0';
      gProgName = StrDup(progname);
      if (which) delete [] which;
      delete[] fullname;
      delete[] progname;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return system error string.

const char *TWinNTSystem::GetError()
{
   Int_t err = GetErrno();
   if (err == 0 && GetLastErrorString() != "")
      return GetLastErrorString();
   if (err < 0 || err >= sys_nerr) {
      static TString error_msg;
      error_msg.Form("errno out of range %d", err);
      return error_msg;
   }
   return sys_errlist[err];
}

////////////////////////////////////////////////////////////////////////////////
/// Return the system's host name.

const char *TWinNTSystem::HostName()
{
   if (fHostname == "")
      fHostname = ::getenv("COMPUTERNAME");
   if (fHostname == "") {
      // This requires a DNS query - but we need it for fallback
      char hn[64];
      DWORD il = sizeof(hn);
      ::GetComputerName(hn, &il);
      fHostname = hn;
   }
   return fHostname;
}

////////////////////////////////////////////////////////////////////////////////
/// Beep. If freq==0 (the default for TWinNTSystem), use ::MessageBeep.
/// Otherwise ::Beep with freq and duration.

void TWinNTSystem::DoBeep(Int_t freq /*=-1*/, Int_t duration /*=-1*/) const
{
   if (freq == 0) {
      ::MessageBeep(-1);
      return;
   }
   if (freq < 37) freq = 440;
   if (duration < 0) duration = 100;
   ::Beep(freq, duration);
}


////////////////////////////////////////////////////////////////////////////////
/// Hook to tell TSystem that the TApplication object has been created.

void TWinNTSystem::NotifyApplicationCreated()
{
   // send a dummy message to the GUI thread to kick it into life
}


////////////////////////////////////////////////////////////////////////////////
/// Add a signal handler to list of system signal handlers. Only adds
/// the handler if it is not already in the list of signal handlers.

void TWinNTSystem::AddSignalHandler(TSignalHandler *h)
{
   Bool_t set_console = kFALSE;
   ESignals  sig = h->GetSignal();

   if (sig == kSigInterrupt) {
      set_console = kTRUE;
      TSignalHandler *hs;
      TIter next(fSignalHandler);

      while ((hs = (TSignalHandler*) next())) {
         if (hs->GetSignal() == kSigInterrupt)
            set_console = kFALSE;
      }
   }
   TSystem::AddSignalHandler(h);

   // Add our handler to the list of the console handlers
   if (set_console)
      ::SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleSigHandler, TRUE);
   else
      WinNTSignal(h->GetSignal(), SigHandler);
}

////////////////////////////////////////////////////////////////////////////////
/// Remove a signal handler from list of signal handlers. Returns
/// the handler or 0 if the handler was not in the list of signal handlers.

TSignalHandler *TWinNTSystem::RemoveSignalHandler(TSignalHandler *h)
{
   if (!h) return 0;

   int sig = h->GetSignal();

   if (sig = kSigInterrupt) {
      Bool_t last = kTRUE;
      TSignalHandler *hs;
      TIter next(fSignalHandler);

      while ((hs = (TSignalHandler*) next())) {
         if (hs->GetSignal() == kSigInterrupt)
            last = kFALSE;
      }
      // Remove our handler from the list of the console handlers
      if (last)
         ::SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleSigHandler, FALSE);
   }
   return TSystem::RemoveSignalHandler(h);
}

////////////////////////////////////////////////////////////////////////////////
/// If reset is true reset the signal handler for the specified signal
/// to the default handler, else restore previous behaviour.

void TWinNTSystem::ResetSignal(ESignals sig, Bool_t reset)
{
   //FIXME!
}

////////////////////////////////////////////////////////////////////////////////
/// Reset signals handlers to previous behaviour.

void TWinNTSystem::ResetSignals()
{
   //FIXME!
}

////////////////////////////////////////////////////////////////////////////////
/// If ignore is true ignore the specified signal, else restore previous
/// behaviour.

void TWinNTSystem::IgnoreSignal(ESignals sig, Bool_t ignore)
{
   // FIXME!
}

////////////////////////////////////////////////////////////////////////////////
/// Print a stack trace, if gEnv entry "Root.Stacktrace" is unset or 1,
/// and if the image helper functions can be found (see InitImagehlpFunctions()).
/// The stack trace is printed for each thread; if fgXcptContext is set (e.g.
/// because there was an exception) use it to define the current thread's context.
/// For each frame in the stack, the frame's module name, the frame's function
/// name, and the frame's line number are printed.

void TWinNTSystem::StackTrace()
{
   if (!gEnv->GetValue("Root.Stacktrace", 1))
      return;

   HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,::GetCurrentProcessId());

   std::cerr.flush();
   fflush (stderr);

   if (!InitImagehlpFunctions()) {
      std::cerr << "No stack trace: cannot find (functions in) dbghelp.dll!" << std::endl;
      return;
   }

   // what system are we on?
   SYSTEM_INFO sysInfo;
   ::GetSystemInfo(&sysInfo);
   DWORD machineType = IMAGE_FILE_MACHINE_I386;
   switch (sysInfo.wProcessorArchitecture) {
      case PROCESSOR_ARCHITECTURE_AMD64:
         machineType = IMAGE_FILE_MACHINE_AMD64;
         break;
      case PROCESSOR_ARCHITECTURE_IA64:
         machineType = IMAGE_FILE_MACHINE_IA64;
         break;
   }

   DWORD currentThreadID = ::GetCurrentThreadId();
   DWORD currentProcessID = ::GetCurrentProcessId();

   if (snapshot == INVALID_HANDLE_VALUE) return;

   THREADENTRY32 threadentry;
   threadentry.dwSize = sizeof(THREADENTRY32);
   if (!::Thread32First(snapshot, &threadentry)) return;

   std::cerr << std::endl << "==========================================" << std::endl;
   std::cerr << "=============== STACKTRACE ===============" << std::endl;
   std::cerr << "==========================================" << std::endl << std::endl;
   UInt_t iThread = 0;
   do {
      if (threadentry.th32OwnerProcessID != currentProcessID)
         continue;
      HANDLE thread = ::OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,
         FALSE, threadentry.th32ThreadID);
      CONTEXT context;
      memset(&context, 0, sizeof(CONTEXT));

      if (threadentry.th32ThreadID != currentThreadID) {
         ::SuspendThread(thread);
         context.ContextFlags = CONTEXT_ALL;
         ::GetThreadContext(thread, &context);
         ::ResumeThread(thread);
      } else {
         if (fgXcptContext) {
            context = *fgXcptContext;
         } else {
            typedef void (WINAPI *RTLCCTXT)(PCONTEXT);
            RTLCCTXT p2RtlCCtxt = (RTLCCTXT) ::GetProcAddress(
               GetModuleHandle("kernel32.dll"), "RtlCaptureContext");
            if (p2RtlCCtxt) {
               context.ContextFlags = CONTEXT_ALL;
               p2RtlCCtxt(&context);
            }
         }
      }

      STACKFRAME64 frame;
      ::ZeroMemory(&frame, sizeof(frame));

      frame.AddrPC.Mode      = AddrModeFlat;
      frame.AddrFrame.Mode   = AddrModeFlat;
      frame.AddrStack.Mode   = AddrModeFlat;
#if defined(_M_IX86)
      frame.AddrPC.Offset    = context.Eip;
      frame.AddrFrame.Offset = context.Ebp;
      frame.AddrStack.Offset = context.Esp;
#elif defined(_M_X64)
      frame.AddrPC.Offset    = context.Rip;
      frame.AddrFrame.Offset = context.Rsp;
      frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IA64)
      frame.AddrPC.Offset    = context.StIIP;
      frame.AddrFrame.Offset = context.IntSp;
      frame.AddrStack.Offset = context.IntSp;
      frame.AddrBStore.Offset= context.RsBSP;
#else
      std::cerr << "Stack traces not supported on your architecture yet." << std::endl;
      return;
#endif

      Bool_t bFirst = kTRUE;
      while (_StackWalk64(machineType, (HANDLE)::GetCurrentProcess(), thread, (LPSTACKFRAME64)&frame,
         (LPVOID)&context, (PREAD_PROCESS_MEMORY_ROUTINE)NULL, (PFUNCTION_TABLE_ACCESS_ROUTINE)_SymFunctionTableAccess64,
         (PGET_MODULE_BASE_ROUTINE)_SymGetModuleBase64, NULL)) {
         if (bFirst)
            std::cerr << std::endl << "================ Thread " << iThread++ << " ================" << std::endl;
         if (!bFirst || threadentry.th32ThreadID != currentThreadID) {
            const std::string moduleName   = GetModuleName(frame.AddrPC.Offset);
            const std::string functionName = GetFunctionName(frame.AddrPC.Offset);
            std::cerr << "  " << moduleName << functionName << std::endl;
         }
         bFirst = kFALSE;
      }
      ::CloseHandle(thread);
   } while (::Thread32Next(snapshot, &threadentry));

   std::cerr << std::endl << "==========================================" << std::endl;
   std::cerr << "============= END STACKTRACE =============" << std::endl;
   std::cerr << "==========================================" << std::endl << std::endl;
   ::CloseHandle(snapshot);
   _SymCleanup(GetCurrentProcess());
}

////////////////////////////////////////////////////////////////////////////////
/// Return the bitmap of conditions that trigger a floating point exception.

Int_t TWinNTSystem::GetFPEMask()
{
   Int_t mask = 0;
   UInt_t oldmask = _statusfp( );

   if (oldmask & _EM_INVALID  )   mask |= kInvalid;
   if (oldmask & _EM_ZERODIVIDE)  mask |= kDivByZero;
   if (oldmask & _EM_OVERFLOW )   mask |= kOverflow;
   if (oldmask & _EM_UNDERFLOW)   mask |= kUnderflow;
   if (oldmask & _EM_INEXACT  )   mask |= kInexact;

   return mask;
}

////////////////////////////////////////////////////////////////////////////////
/// Set which conditions trigger a floating point exception.
/// Return the previous set of conditions.

Int_t TWinNTSystem::SetFPEMask(Int_t mask)
{
   Int_t old = GetFPEMask();

   UInt_t newm = 0;
   if (mask & kInvalid  )   newm |= _EM_INVALID;
   if (mask & kDivByZero)   newm |= _EM_ZERODIVIDE;
   if (mask & kOverflow )   newm |= _EM_OVERFLOW;
   if (mask & kUnderflow)   newm |= _EM_UNDERFLOW;
   if (mask & kInexact  )   newm |= _EM_INEXACT;

   UInt_t cm = ::_statusfp();
   cm &= ~newm;
   ::_controlfp(cm , _MCW_EM);

   return old;
}

//---- handling of system events -----------------------------------------------
////////////////////////////////////////////////////////////////////////////////
/// Handle and dispatch signals.

void TWinNTSystem::DispatchSignals(ESignals sig)
{
   if (sig == kSigInterrupt) {
      fSignals->Set(sig);
      fSigcnt++;
   }
   else {
      if (gExceptionHandler) {
         //sig is ESignal, should it be mapped to the correct signal number?
         if (sig == kSigFloatingException) _fpreset();
         gExceptionHandler->HandleException(sig);
      } else {
         //map to the real signal code + set the
         //high order bit to indicate a signal (?)
         StackTrace();
         if (TROOT::Initialized()) {
             ::CppyyLegacy::Throw(sig);
         }
      }
      Abort(-1);
   }

   // check a-synchronous signals
   if (fSigcnt > 0 && fSignalHandler->GetSize() > 0)
      CheckSignals(kFALSE);
}

////////////////////////////////////////////////////////////////////////////////
/// Check if some signals were raised and call their Notify() member.

Bool_t TWinNTSystem::CheckSignals(Bool_t sync)
{
   TSignalHandler *sh;
   Int_t sigdone = -1;
   {
      TIter next(fSignalHandler);

      while (sh = (TSignalHandler*)next()) {
         if (sync == sh->IsSync()) {
            ESignals sig = sh->GetSignal();
            if ((fSignals->IsSet(sig) && sigdone == -1) || sigdone == sig) {
               if (sigdone == -1) {
                  fSignals->Clr(sig);
                  sigdone = sig;
                  fSigcnt--;
               }
               sh->Notify();
            }
         }
      }
   }
   if (sigdone != -1) return kTRUE;

   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Check if there is activity on some file descriptors and call their
/// Notify() member.

Bool_t TWinNTSystem::CheckDescriptors()
{
   return kFALSE;
}

//---- Directories -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Make a file system directory. Returns 0 in case of success and
/// -1 if the directory could not be created (either already exists or
/// illegal path name).
/// If 'recursive' is true, makes parent directories as needed.

int TWinNTSystem::mkdir(const char *name, Bool_t recursive)
{
   if (recursive) {
      TString dirname = DirName(name);
      if (dirname.Length() == 0) {
         // well we should not have to make the root of the file system!
         // (and this avoid infinite recursions!)
         return 0;
      }
      if (IsAbsoluteFileName(name)) {
         // For some good reason DirName strips off the drive letter
         // (if present), we need it to make the directory on the
         // right disk, so let's put it back!
         const char driveletter = DriveName(name);
         if (driveletter) {
            dirname.Prepend(":");
            dirname.Prepend(driveletter);
         }
      }
      if (AccessPathName(dirname, kFileExists)) {
         int res = this->mkdir(dirname, kTRUE);
         if (res) return res;
      }
      if (!AccessPathName(name, kFileExists)) {
         return -1;
      }
   }
   return MakeDirectory(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Make a WinNT file system directory. Returns 0 in case of success and
/// -1 if the directory could not be created (either already exists or
/// illegal path name).

int  TWinNTSystem::MakeDirectory(const char *name)
{
   TSystem *helper = FindHelper(name);
   if (helper) {
      return helper->MakeDirectory(name);
   }
   const char *proto = (strstr(name, "file:///")) ? "file://" : "file:";
#ifdef WATCOM
   // It must be as follows
   if (!name) return 0;
   return ::mkdir(StripOffProto(name, proto));
#else
   // but to be in line with TUnixSystem I did like this
   if (!name) return 0;
   return ::_mkdir(StripOffProto(name, proto));
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Close a WinNT file system directory.

void TWinNTSystem::FreeDirectory(void *dirp)
{
   TSystem *helper = FindHelper(0, dirp);
   if (helper) {
      helper->FreeDirectory(dirp);
      return;
   }

   if (dirp) {
      ::FindClose(dirp);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the next directory entry.

const char *TWinNTSystem::GetDirEntry(void *dirp)
{
   TSystem *helper = FindHelper(0, dirp);
   if (helper) {
      return helper->GetDirEntry(dirp);
   }

   if (dirp) {
      HANDLE searchFile = (HANDLE)dirp;
      if (fFirstFile) {
         // when calling TWinNTSystem::OpenDirectory(), the fFindFileData
         // structure is filled by a call to FindFirstFile().
         // So first returns this one, before calling FindNextFile()
         fFirstFile = kFALSE;
         return (const char *)fFindFileData.cFileName;
      }
      if (::FindNextFile(searchFile, &fFindFileData)) {
         return (const char *)fFindFileData.cFileName;
      }
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Change directory.

Bool_t TWinNTSystem::ChangeDirectory(const char *path)
{
   Bool_t ret = (Bool_t) (::chdir(path) == 0);
   if (fWdpath != "")
      fWdpath = "";   // invalidate path cache
   return ret;
}

////////////////////////////////////////////////////////////////////////////////
///
/// Inline function to check for a double-backslash at the
/// beginning of a string
///

__inline BOOL DBL_BSLASH(LPCTSTR psz)
{
   return (psz[0] == TEXT('\\') && psz[1] == TEXT('\\'));
}

////////////////////////////////////////////////////////////////////////////////
/// Returns TRUE if the given string is a UNC path.
///
/// TRUE
///      "\\foo\bar"
///      "\\foo"         <- careful
///      "\\"
/// FALSE
///      "\foo"
///      "foo"
///      "c:\foo"

BOOL PathIsUNC(LPCTSTR pszPath)
{
   return DBL_BSLASH(pszPath);
}

#pragma data_seg(".text", "CODE")
const TCHAR c_szColonSlash[] = TEXT(":\\");
#pragma data_seg()

////////////////////////////////////////////////////////////////////////////////
///
/// check if a path is a root
///
/// returns:
///  TRUE for "\" "X:\" "\\foo\asdf" "\\foo\"
///  FALSE for others
///

BOOL PathIsRoot(LPCTSTR pPath)
{
   if (!IsDBCSLeadByte(*pPath)) {
      if (!lstrcmpi(pPath + 1, c_szColonSlash))
         // "X:\" case
         return TRUE;
   }
   if ((*pPath == TEXT('\\')) && (*(pPath + 1) == 0))
      // "\" case
      return TRUE;
   if (DBL_BSLASH(pPath)) {
      // smells like UNC name
      LPCTSTR p;
      int cBackslashes = 0;
      for (p = pPath + 2; *p; p = CharNext(p)) {
         if (*p == TEXT('\\') && (++cBackslashes > 1))
            return FALSE;   // not a bare UNC name, therefore not a root dir
      }
      // end of string with only 1 more backslash
      // must be a bare UNC, which looks like a root dir
      return TRUE;
   }
   return FALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Open a directory. Returns 0 if directory does not exist.

void *TWinNTSystem::OpenDirectory(const char *fdir)
{
   TSystem *helper = FindHelper(fdir);
   if (helper) {
      return helper->OpenDirectory(fdir);
   }

   const char *proto = (strstr(fdir, "file:///")) ? "file://" : "file:";
   const char *sdir = StripOffProto(fdir, proto);

   char *dir = new char[MAX_PATH];
   if (IsShortcut(sdir)) {
      if (!ResolveShortCut(sdir, dir, MAX_PATH))
         strlcpy(dir, sdir,MAX_PATH);
   }
   else
      strlcpy(dir, sdir,MAX_PATH);

   int nche = strlen(dir)+3;
   char *entry = new char[nche];
   struct _stati64 finfo;

   if(PathIsUNC(dir)) {
      strlcpy(entry, dir,nche);
      if ((entry[strlen(dir)-1] == '/') || (entry[strlen(dir)-1] == '\\' )) {
         entry[strlen(dir)-1] = '\0';
      }
      if(PathIsRoot(entry)) {
         strlcat(entry,"\\",nche);
      }
      if (_stati64(entry, &finfo) < 0) {
         delete [] entry;
         delete [] dir;
         return 0;
      }
   }
   else {
      strlcpy(entry, dir,nche);
      if ((entry[strlen(dir)-1] == '/') || (entry[strlen(dir)-1] == '\\' )) {
         if(!PathIsRoot(entry))
            entry[strlen(dir)-1] = '\0';
      }
      if (_stati64(entry, &finfo) < 0) {
         delete [] entry;
         delete [] dir;
         return 0;
      }
   }

   if (finfo.st_mode & S_IFDIR) {
      strlcpy(entry, dir,nche);
      if (!(entry[strlen(dir)-1] == '/' || entry[strlen(dir)-1] == '\\' )) {
         strlcat(entry,"\\",nche);
      }
      if (entry[strlen(dir)-1] == ' ')
         entry[strlen(dir)-1] = '\0';
      strlcat(entry,"*",nche);

      HANDLE searchFile;
      searchFile = ::FindFirstFile(entry, &fFindFileData);
      if (searchFile == INVALID_HANDLE_VALUE) {
         ((TWinNTSystem *)gSystem)->Error( "Unable to find' for reading:", entry);
         delete [] entry;
         delete [] dir;
         return 0;
      }
      delete [] entry;
      delete [] dir;
      fFirstFile = kTRUE;
      return searchFile;
   } else {
      delete [] entry;
      delete [] dir;
      return 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return the working directory for the default drive

const char *TWinNTSystem::WorkingDirectory()
{
   return WorkingDirectory('\0');
}

//////////////////////////////////////////////////////////////////////////////
/// Return the working directory for the default drive

std::string TWinNTSystem::GetWorkingDirectory() const
{
   char *wdpath = GetWorkingDirectory('\0');
   std::string cwd;
   if (wdpath) {
      cwd = wdpath;
      free(wdpath);
   }
   return cwd;
}

////////////////////////////////////////////////////////////////////////////////
///  Return working directory for the selected drive
///  driveletter == 0 means return the working durectory for the default drive

const char *TWinNTSystem::WorkingDirectory(char driveletter)
{
   char *wdpath = GetWorkingDirectory(driveletter);
   if (wdpath) {
      fWdpath = wdpath;

      // Make sure the drive letter is upper case
      if (fWdpath[1] == ':')
         fWdpath[0] = toupper(fWdpath[0]);

      free(wdpath);
   }
   return fWdpath;
}

//////////////////////////////////////////////////////////////////////////////
///  Return working directory for the selected drive (helper function).
///  The caller must free the return value.

char *TWinNTSystem::GetWorkingDirectory(char driveletter) const
{
   char *wdpath = 0;
   char drive = driveletter ? toupper( driveletter ) - 'A' + 1 : 0;

   // don't use cache as user can call chdir() directly somewhere else
   //if (fWdpath != "" )
   //   return fWdpath;

   if (!(wdpath = ::_getdcwd( (int)drive, wdpath, kMAXPATHLEN))) {
      free(wdpath);
      Warning("WorkingDirectory", "getcwd() failed");
      return 0;
   }

   return wdpath;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the user's home directory.

const char *TWinNTSystem::HomeDirectory(const char *userName)
{
   static char mydir[kMAXPATHLEN] = "./";
   FillWithHomeDirectory(userName, mydir);
   return mydir;
}

//////////////////////////////////////////////////////////////////////////////
/// Return the user's home directory.

std::string TWinNTSystem::GetHomeDirectory(const char *userName) const
{
   char mydir[kMAXPATHLEN] = "./";
   FillWithHomeDirectory(userName, mydir);
   return std::string(mydir); 
}

//////////////////////////////////////////////////////////////////////////////
/// Fill buffer with user's home directory.

void TWinNTSystem::FillWithHomeDirectory(const char *userName, char *mydir) const
{
   const char *h = 0;
   if (!(h = ::getenv("home"))) h = ::getenv("HOME");

   if (h) {
      strlcpy(mydir, h,kMAXPATHLEN);
   } else {
      // for Windows NT HOME might be defined as either $(HOMESHARE)/$(HOMEPATH)
      //                                         or     $(HOMEDRIVE)/$(HOMEPATH)
      h = ::getenv("HOMESHARE");
      if (!h)  h = ::getenv("HOMEDRIVE");
      if (h) {
         strlcpy(mydir, h,kMAXPATHLEN);
         h = ::getenv("HOMEPATH");
         if(h) strlcat(mydir, h,kMAXPATHLEN);
      }
      // on Windows Vista HOME is usually defined as $(USERPROFILE)
      if (!h) {
         h = ::getenv("USERPROFILE");
         if (h) strlcpy(mydir, h,kMAXPATHLEN);
      }
   }
   // Make sure the drive letter is upper case
   if (mydir[1] == ':')
      mydir[0] = toupper(mydir[0]);
}


////////////////////////////////////////////////////////////////////////////////
/// Return a user configured or systemwide directory to create
/// temporary files in.

const char *TWinNTSystem::TempDirectory() const
{
   const char *dir =  gSystem->Getenv("TEMP");
   if (!dir)   dir =  gSystem->Getenv("TEMPDIR");
   if (!dir)   dir =  gSystem->Getenv("TEMP_DIR");
   if (!dir)   dir =  gSystem->Getenv("TMP");
   if (!dir)   dir =  gSystem->Getenv("TMPDIR");
   if (!dir)   dir =  gSystem->Getenv("TMP_DIR");
   if (!dir) dir = "c:\\";

   return dir;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a secure temporary file by appending a unique
/// 6 letter string to base. The file will be created in
/// a standard (system) directory or in the directory
/// provided in dir. The full filename is returned in base
/// and a filepointer is returned for safely writing to the file
/// (this avoids certain security problems). Returns 0 in case
/// of error.

FILE *TWinNTSystem::TempFileName(TString &base, const char *dir)
{
   char tmpName[MAX_PATH];

   ::GetTempFileName(dir ? dir : TempDirectory(), base.Data(), 0, tmpName);
   base = tmpName;
   FILE *fp = fopen(tmpName, "w+");

   if (!fp) ::CppyyLegacy::SysError("TempFileName", "error opening %s", tmpName);

   return fp;
}

//---- Paths & Files -----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get list of volumes (drives) mounted on the system.
/// The returned TList must be deleted by the user using "delete".

TList *TWinNTSystem::GetVolumes(Option_t *opt) const
{
   Int_t   curdrive;
   UInt_t  type;
   TString sDrive, sType;
   char    szFs[32];

   if (!opt || !opt[0]) {
      return 0;
   }

   // prevent the system dialog box to pop-up if a drive is empty
   UINT nOldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
   TList *drives = new TList();
   drives->SetOwner();
   // Save current drive
   curdrive = _getdrive();
   if (strstr(opt, "cur")) {
      *szFs='\0';
      sDrive.Form("%c:", (curdrive + 'A' - 1));
      sType.Form("Unknown Drive (%s)", sDrive.Data());
      ::GetVolumeInformation(Form("%s\\", sDrive.Data()), NULL, 0, NULL, NULL,
                             NULL, (LPSTR)szFs, 32);
      type = ::GetDriveType(sDrive.Data());
      switch (type) {
         case DRIVE_UNKNOWN:
         case DRIVE_NO_ROOT_DIR:
            break;
         case DRIVE_REMOVABLE:
            sType.Form("Removable Disk (%s)", sDrive.Data());
            break;
         case DRIVE_FIXED:
            sType.Form("Local Disk (%s)", sDrive.Data());
            break;
         case DRIVE_REMOTE:
            sType.Form("Network Drive (%s) (%s)", szFs, sDrive.Data());
            break;
         case DRIVE_CDROM:
            sType.Form("CD/DVD Drive (%s)", sDrive.Data());
            break;
         case DRIVE_RAMDISK:
            sType.Form("RAM Disk (%s)", sDrive.Data());
            break;
      }
      drives->Add(new TNamed(sDrive.Data(), sType.Data()));
   }
   else if (strstr(opt, "all")) {
      TCHAR szTemp[512];
      szTemp[0] = '\0';
      if (::GetLogicalDriveStrings(511, szTemp)) {
         TCHAR szDrive[3] = TEXT(" :");
         TCHAR* p = szTemp;
         do {
            // Copy the drive letter to the template string
            *szDrive = *p;
            *szFs='\0';
            sDrive.Form("%s", szDrive);
            // skip floppy drives, to avoid accessing them each time...
            if ((sDrive == "A:") || (sDrive == "B:")) {
               while (*p++);
               continue;
            }
            sType.Form("Unknown Drive (%s)", sDrive.Data());
            ::GetVolumeInformation(Form("%s\\", sDrive.Data()), NULL, 0, NULL,
                                   NULL, NULL, (LPSTR)szFs, 32);
            type = ::GetDriveType(sDrive.Data());
            switch (type) {
               case DRIVE_UNKNOWN:
               case DRIVE_NO_ROOT_DIR:
                  break;
               case DRIVE_REMOVABLE:
                  sType.Form("Removable Disk (%s)", sDrive.Data());
                  break;
               case DRIVE_FIXED:
                  sType.Form("Local Disk (%s)", sDrive.Data());
                  break;
               case DRIVE_REMOTE:
                  sType.Form("Network Drive (%s) (%s)", szFs, sDrive.Data());
                  break;
               case DRIVE_CDROM:
                  sType.Form("CD/DVD Drive (%s)", sDrive.Data());
                  break;
               case DRIVE_RAMDISK:
                  sType.Form("RAM Disk (%s)", sDrive.Data());
                  break;
            }
            drives->Add(new TNamed(sDrive.Data(), sType.Data()));
            // Go to the next NULL character.
            while (*p++);
         } while (*p); // end of string
      }
   }
   // restore previous error mode
   ::SetErrorMode(nOldErrorMode);
   return drives;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the directory name in pathname. DirName of c:/user/root is /user.
/// It creates output with 'new char []' operator. Returned string has to
/// be deleted.

const char *TWinNTSystem::DirName(const char *pathname)
{
   // Delete old buffer
   if (fDirNameBuffer) {
      // delete [] fDirNameBuffer;
      fDirNameBuffer = 0;
   }

   // Create a buffer to keep the path name
   if (pathname) {
      if (strchr(pathname, '/') || strchr(pathname, '\\')) {
         const char *rslash = strrchr(pathname, '/');
         const char *bslash = strrchr(pathname, '\\');
         const char *r = (std::max)(rslash, bslash);
         const char *ptr = pathname;
         while (ptr <= r) {
            if (*ptr == ':') {
               // Windows path may contain a drive letter
               // For NTFS ":" may be a "stream" delimiter as well
               pathname =  ptr + 1;
               break;
            }
            ptr++;
         }
         int len =  r - pathname;
         if (len > 0) {
            fDirNameBuffer = new char[len+1];
            memcpy(fDirNameBuffer, pathname, len);
            fDirNameBuffer[len] = 0;
         }
      }
   }
   if (!fDirNameBuffer) {
      fDirNameBuffer = new char[1];
      *fDirNameBuffer = '\0'; // Set the empty default response
   }
   return fDirNameBuffer;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the drive letter in pathname. DriveName of 'c:/user/root' is 'c'
///
///   Input:
///     - pathname - the string containing file name
///
///   Return:
///     - Letter representing the drive letter in the file name
///     - The current drive if the pathname has no drive assigment
///     - 0 if pathname is an empty string  or uses UNC syntax
///
///   Note:
///      It doesn't check whether pathname represents a 'real' filename.
///      This subroutine looks for 'single letter' followed by a ':'.

const char TWinNTSystem::DriveName(const char *pathname)
{
   if (!pathname)    return 0;
   if (!pathname[0]) return 0;

   const char *lpchar;
   lpchar = pathname;

   // Skip blanks
   while(*lpchar == ' ') lpchar++;

   if (isalpha((int)*lpchar) && *(lpchar+1) == ':') {
      return *lpchar;
   }
   // Test UNC syntax
   if ( (*lpchar == '\\' || *lpchar == '/' ) &&
        (*(lpchar+1) == '\\' || *(lpchar+1) == '/') ) return 0;

   // return the current drive
   return DriveName(WorkingDirectory());
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if dir is an absolute pathname.

Bool_t TWinNTSystem::IsAbsoluteFileName(const char *dir)
{
   if (dir) {
      int idx = 0;
      if (strchr(dir,':')) idx = 2;
      return  (dir[idx] == '/' || dir[idx] == '\\');
   }
   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Convert a pathname to a unix pathname. E.g. form \user\root to /user/root.
/// General rules for applications creating names for directories and files or
/// processing names supplied by the user include the following:
///
///  *  Use any character in the current code page for a name, but do not use
///     a path separator, a character in the range 0 through 31, or any character
///     explicitly disallowed by the file system. A name can contain characters
///     in the extended character set (128-255).
///  *  Use the backslash (\), the forward slash (/), or both to separate
///     components in a path. No other character is acceptable as a path separator.
///  *  Use a period (.) as a directory component in a path to represent the
///     current directory.
///  *  Use two consecutive periods (..) as a directory component in a path to
///     represent the parent of the current directory.
///  *  Use a period (.) to separate components in a directory name or filename.
///  *  Do not use the following characters in directory names or filenames, because
///     they are reserved for Windows:
///                      < > : " / \ |
///  *  Do not use reserved words, such as aux, con, and prn, as filenames or
///     directory names.
///  *  Process a path as a null-terminated string. The maximum length for a path
///     is given by MAX_PATH.
///  *  Do not assume case sensitivity. Consider names such as OSCAR, Oscar, and
///     oscar to be the same.

const char *TWinNTSystem::UnixPathName(const char *name)
{
   const int kBufSize = 1024;
   TTHREAD_TLS_ARRAY(char, kBufSize, temp);

   strlcpy(temp, name, kBufSize);
   char *currentChar = temp;

   // This can not change the size of the string.
   while (*currentChar != '\0') {
      if (*currentChar == '\\') *currentChar = '/';
      currentChar++;
   }
   return temp;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns FALSE if one can access a file using the specified access mode.
/// Mode is the same as for the WinNT access(2) function.
/// Attention, bizarre convention of return value!!

Bool_t TWinNTSystem::AccessPathName(const char *path, EAccessMode mode)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->AccessPathName(path, mode);

   // prevent the system dialog box to pop-up if a drive is empty
   UINT nOldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
   if (mode==kExecutePermission)
      // cannot test on exe - use read instead
      mode=kReadPermission;
   const char *proto = (strstr(path, "file:///")) ? "file://" : "file:";
   if (::_access(StripOffProto(path, proto), mode) == 0) {
      // restore previous error mode
      ::SetErrorMode(nOldErrorMode);
      return kFALSE;
   }
   GetLastErrorString() = GetError();
   // restore previous error mode
   ::SetErrorMode(nOldErrorMode);
   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns TRUE if the url in 'path' points to the local file system.
/// This is used to avoid going through the NIC card for local operations.

Bool_t TWinNTSystem::IsPathLocal(const char *path)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->IsPathLocal(path);

   return TSystem::IsPathLocal(path);
}

////////////////////////////////////////////////////////////////////////////////
/// Concatenate a directory and a file name.

const char *TWinNTSystem::PrependPathName(const char *dir, TString& name)
{
   if (name == ".") name = "";
   if (dir && dir[0]) {
      // Test whether the last symbol of the directory is a separator
      char last = dir[strlen(dir) - 1];
      if (last != '/' && last != '\\') {
         name.Prepend('\\');
      }
      name.Prepend(dir);
      name.ReplaceAll("/", "\\");
   }
   return name.Data();
}

////////////////////////////////////////////////////////////////////////////////
/// Copy a file. If overwrite is true and file already exists the
/// file will be overwritten. Returns 0 when successful, -1 in case
/// of failure, -2 in case the file already exists and overwrite was false.

int TWinNTSystem::CopyFile(const char *f, const char *t, Bool_t overwrite)
{
   if (AccessPathName(f, kReadPermission)) return -1;
   if (!AccessPathName(t) && !overwrite) return -2;

   Bool_t ret = ::CopyFileA(f, t, kFALSE);

   if (!ret) return -1;
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Rename a file. Returns 0 when successful, -1 in case of failure.

int TWinNTSystem::Rename(const char *f, const char *t)
{
   int ret = ::rename(f, t);
   GetLastErrorString() = GetError();
   return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// Get info about a file. Info is returned in the form of a FileStat_t
/// structure (see TSystem.h).
/// The function returns 0 in case of success and 1 if the file could
/// not be stat'ed.

int TWinNTSystem::GetPathInfo(const char *path, FileStat_t &buf)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->GetPathInfo(path, buf);

   struct _stati64 sbuf;

   // Remove trailing backslashes
   const char *proto = (strstr(path, "file:///")) ? "file://" : "file:";
   char *newpath = StrDup(StripOffProto(path, proto));
   int l = strlen(newpath);
   while (l > 1) {
      if (newpath[--l] != '\\' || newpath[--l] != '/') {
         break;
      }
      newpath[l] = '\0';
   }

   if (newpath && ::_stati64(newpath, &sbuf) >= 0) {

      buf.fDev    = sbuf.st_dev;
      buf.fIno    = sbuf.st_ino;
      buf.fMode   = sbuf.st_mode;
      buf.fUid    = sbuf.st_uid;
      buf.fGid    = sbuf.st_gid;
      buf.fSize   = sbuf.st_size;
      buf.fMtime  = sbuf.st_mtime;
      buf.fIsLink = IsShortcut(newpath); // kFALSE;

      char *lpath = new char[MAX_PATH];
      if (IsShortcut(newpath)) {
         struct _stati64 sbuf2;
         if (ResolveShortCut(newpath, lpath, MAX_PATH)) {
            if (::_stati64(lpath, &sbuf2) >= 0) {
               buf.fMode   = sbuf2.st_mode;
            }
         }
      }
      delete [] lpath;

      delete [] newpath;
      return 0;
   }
   delete [] newpath;
   return 1;
}

////////////////////////////////////////////////////////////////////////////////
/// Get info about a file system: id, bsize, bfree, blocks.
/// Id      is file system type (machine dependend, see statfs())
/// Bsize   is block size of file system
/// Blocks  is total number of blocks in file system
/// Bfree   is number of free blocks in file system
/// The function returns 0 in case of success and 1 if the file system could
/// not be stat'ed.

int TWinNTSystem::GetFsInfo(const char *path, Long_t *id, Long_t *bsize,
                            Long_t *blocks, Long_t *bfree)
{
   // address of root directory of the file system
   LPCTSTR lpRootPathName = path;

   // address of name of the volume
   LPTSTR  lpVolumeNameBuffer = 0;
   DWORD   nVolumeNameSize = 0;

   DWORD   volumeSerialNumber;     // volume serial number
   DWORD   maximumComponentLength; // system's maximum filename length

   // file system flags
   DWORD fileSystemFlags;

   // address of name of file system
   char  fileSystemNameBuffer[512];
   DWORD nFileSystemNameSize = sizeof(fileSystemNameBuffer);

   // prevent the system dialog box to pop-up if the drive is empty
   UINT nOldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
   if (!::GetVolumeInformation(lpRootPathName,
                               lpVolumeNameBuffer, nVolumeNameSize,
                               &volumeSerialNumber,
                               &maximumComponentLength,
                               &fileSystemFlags,
                               fileSystemNameBuffer, nFileSystemNameSize)) {
      // restore previous error mode
      ::SetErrorMode(nOldErrorMode);
      return 1;
   }

   const char *fsNames[] = { "FAT", "NTFS" };
   int i;
   for (i = 0; i < 2; i++) {
      if (!strncmp(fileSystemNameBuffer, fsNames[i], nFileSystemNameSize))
         break;
   }
   *id = i;

   DWORD sectorsPerCluster;      // # sectors per cluster
   DWORD bytesPerSector;         // # bytes per sector
   DWORD numberOfFreeClusters;   // # free clusters
   DWORD totalNumberOfClusters;  // # total of clusters

   if (!::GetDiskFreeSpace(lpRootPathName,
                           &sectorsPerCluster,
                           &bytesPerSector,
                           &numberOfFreeClusters,
                           &totalNumberOfClusters)) {
      // restore previous error mode
      ::SetErrorMode(nOldErrorMode);
      return 1;
   }
   // restore previous error mode
   ::SetErrorMode(nOldErrorMode);

   *bsize  = sectorsPerCluster * bytesPerSector;
   *blocks = totalNumberOfClusters;
   *bfree  = numberOfFreeClusters;

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a link from file1 to file2.

int TWinNTSystem::Link(const char *from, const char *to)
{
   struct   _stati64 finfo;
   char     winDrive[256];
   char     winDir[256];
   char     winName[256];
   char     winExt[256];
   char     linkname[1024];
   LPTSTR   lpszFilePart;
   TCHAR    szPath[MAX_PATH];
   DWORD    dwRet = 0;

   typedef BOOL (__stdcall *CREATEHARDLINKPROC)( LPCTSTR, LPCTSTR, LPSECURITY_ATTRIBUTES );
   static CREATEHARDLINKPROC _CreateHardLink = 0;

   HMODULE hModImagehlp = LoadLibrary( "Kernel32.dll" );
   if (!hModImagehlp)
      return -1;

#ifdef _UNICODE
   _CreateHardLink = (CREATEHARDLINKPROC) GetProcAddress( hModImagehlp, "CreateHardLinkW" );
#else
   _CreateHardLink = (CREATEHARDLINKPROC) GetProcAddress( hModImagehlp, "CreateHardLinkA" );
#endif
   if (!_CreateHardLink)
      return -1;

   dwRet = GetFullPathName(from, sizeof(szPath) / sizeof(TCHAR),
                           szPath, &lpszFilePart);

   if (_stati64(szPath, &finfo) < 0)
      return -1;

   if (finfo.st_mode & S_IFDIR)
      return -1;

   snprintf(linkname,1024,"%s",to);
   _splitpath(linkname,winDrive,winDir,winName,winExt);
   if ((!winDrive[0] ) &&
       (!winDir[0] ))  {
      _splitpath(szPath,winDrive,winDir,winName,winExt);
      snprintf(linkname,1024,"%s\\%s\\%s", winDrive, winDir, to);
   }
   else if (!winDrive[0])  {
      _splitpath(szPath,winDrive,winDir,winName,winExt);
      snprintf(linkname,1024,"%s\\%s", winDrive, to);
   }

   if (!_CreateHardLink(linkname, szPath, NULL))
      return -1;

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Create a symlink from file1 to file2. Returns 0 when successful,
/// -1 in case of failure.

int TWinNTSystem::Symlink(const char *from, const char *to)
{
   HRESULT        hRes;                  /* Returned COM result code */
   IShellLink*    pShellLink;            /* IShellLink object pointer */
   IPersistFile*  pPersistFile;          /* IPersistFile object pointer */
   WCHAR          wszLinkfile[MAX_PATH]; /* pszLinkfile as Unicode string */
   int            iWideCharsWritten;     /* Number of wide characters written */
   DWORD          dwRet = 0;
   LPTSTR         lpszFilePart;
   TCHAR          szPath[MAX_PATH];

   hRes = E_INVALIDARG;
   if ((from == NULL) || (!from[0]) || (to == NULL) ||
       (!to[0]))
      return -1;

   // Make typedefs for some ole32.dll functions so that we can use them
   // with GetProcAddress
   typedef HRESULT (__stdcall *COINITIALIZEPROC)( LPVOID );
   static COINITIALIZEPROC _CoInitialize = 0;
   typedef void (__stdcall *COUNINITIALIZEPROC)( void );
   static COUNINITIALIZEPROC _CoUninitialize = 0;
   typedef HRESULT (__stdcall *COCREATEINSTANCEPROC)( REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID );
   static COCREATEINSTANCEPROC _CoCreateInstance = 0;

   HMODULE hModImagehlp = LoadLibrary( "ole32.dll" );
   if (!hModImagehlp)
      return -1;

   _CoInitialize = (COINITIALIZEPROC) GetProcAddress( hModImagehlp, "CoInitialize" );
   if (!_CoInitialize)
      return -1;
   _CoUninitialize = (COUNINITIALIZEPROC) GetProcAddress( hModImagehlp, "CoUninitialize" );
   if (!_CoUninitialize)
      return -1;
   _CoCreateInstance = (COCREATEINSTANCEPROC) GetProcAddress( hModImagehlp, "CoCreateInstance" );
   if (!_CoCreateInstance)
      return -1;

   TString linkname(to);
   if (!linkname.EndsWith(".lnk"))
      linkname.Append(".lnk");

   _CoInitialize(NULL);

   // Retrieve the full path and file name of a specified file
   dwRet = GetFullPathName(from, sizeof(szPath) / sizeof(TCHAR),
                           szPath, &lpszFilePart);
   hRes = _CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                           IID_IShellLink, (LPVOID *)&pShellLink);
   if (SUCCEEDED(hRes)) {
      // Set the fields in the IShellLink object
      hRes = pShellLink->SetPath(szPath);
      // Use the IPersistFile object to save the shell link
      hRes = pShellLink->QueryInterface(IID_IPersistFile, (void **)&pPersistFile);
      if (SUCCEEDED(hRes)){
         iWideCharsWritten = MultiByteToWideChar(CP_ACP, 0, linkname.Data(), -1,
                                                 wszLinkfile, MAX_PATH);
         hRes = pPersistFile->Save(wszLinkfile, TRUE);
         pPersistFile->Release();
      }
      pShellLink->Release();
   }
   _CoUninitialize();
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Unlink, i.e. remove, a file or directory.
///
/// If the file is currently open by the current or another process Windows does not allow the file to be deleted and
/// the operation is a no-op.

int TWinNTSystem::Unlink(const char *name)
{
   TSystem *helper = FindHelper(name);
   if (helper)
      return helper->Unlink(name);

   struct _stati64 finfo;

   if (_stati64(name, &finfo) < 0) {
      return -1;
   }

   if (finfo.st_mode & S_IFDIR) {
      return ::_rmdir(name);
   } else {
      return ::_unlink(name);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Make descriptor fd non-blocking.

int TWinNTSystem::SetNonBlock(int fd)
{
   if (::ioctlsocket(fd, FIONBIO, (u_long *)1) == SOCKET_ERROR) {
      ::CppyyLegacy::SysError("SetNonBlock", "ioctlsocket");
      return -1;
   }
   return 0;
}

// expand the metacharacters as in the shell

static const char
   *shellMeta  = "~*[]{}?$%",
   *shellStuff = "(){}<>\"'",
   shellEscape = '\\';

////////////////////////////////////////////////////////////////////////////////
/// Expand a pathname getting rid of special shell characaters like ~.$, etc.

Bool_t TWinNTSystem::ExpandPathName(TString &patbuf0)
{
   const char *patbuf = (const char *)patbuf0;
   const char *p;
   char   *cmd = 0;
   char  *q;

   Int_t old_level = gErrorIgnoreLevel;
   gErrorIgnoreLevel = kFatal; // Explicitly remove all messages
   if (patbuf0.BeginsWith("\\")) {
      const char driveletter = DriveName(patbuf);
      if (driveletter) {
         patbuf0.Prepend(":");
         patbuf0.Prepend(driveletter);
      }
   }
   TUrl urlpath(patbuf0, kTRUE);
   TString proto = urlpath.GetProtocol();
   gErrorIgnoreLevel = old_level;
   if (!proto.EqualTo("file")) // don't expand urls!!!
      return kFALSE;

   // skip the "file:" protocol, if any
   if (patbuf0.BeginsWith("file:"))
      patbuf += 5;

   // skip leading blanks
   while (*patbuf == ' ') {
      patbuf++;
   }

   // skip leading ':'
   while (*patbuf == ':') {
      patbuf++;
   }

   // skip leading ';'
   while (*patbuf == ';') {
      patbuf++;
   }

   // Transform a Unix list of directories into a Windows list
   // by changing the separator from ':' into ';'
   for (q = (char*)patbuf; *q; q++) {
      if ( *q == ':' ) {
         // We are avoiding substitution in the case of
         // ....;c:.... and of ...;root:/... where root can be any url protocol
         if ( (((q-2)>patbuf) && ( (*(q-2)!=';') || !isalpha(*(q-1)) )) &&
              *(q+1)!='/' ) {
            *q=';';
         }
      }
   }
   // any shell meta characters ?
   for (p = patbuf; *p; p++) {
      if (strchr(shellMeta, *p)) {
         goto needshell;
      }
   }
   return kFALSE;

needshell:

   // Because (problably) we built with cygwin, the path name like:
   //     LOCALS~1\\Temp
   // gets extended to
   //     LOCALSc:\\Devel
   // The most likely cause is that '~' is used with Unix semantic of the
   // home directory (and it also cuts the path short after ... who knows why!)
   // So we need to detect this case and prevents its expansion :(.

   char replacement[4];

   // intentionally a non visible, unlikely character
   for (int k = 0; k<3; k++) replacement[k] = 0x1;

   replacement[3] = 0x0;
   Ssiz_t pos = 0;
   TRegexp TildaNum = "~[0-9]";

   while ( (pos = patbuf0.Index(TildaNum,pos)) != kNPOS ) {
      patbuf0.Replace(pos, 1, replacement);
   }

   // escape shell quote characters
   // EscChar(patbuf, stuffedPat, sizeof(stuffedPat), shellStuff, shellEscape);
   ExpandFileName(patbuf0);
   Int_t lbuf = ::ExpandEnvironmentStrings(
                                 patbuf0.Data(), // pointer to string with environment variables
                                 cmd,            // pointer to string with expanded environment variables
                                 0               // maximum characters in expanded string
                              );
   if (lbuf > 0) {
      cmd = new char[lbuf+1];
      ::ExpandEnvironmentStrings(
                               patbuf0.Data(), // pointer to string with environment variables
                               cmd,            // pointer to string with expanded environment variables
                               lbuf            // maximum characters in expanded string
                               );
      patbuf0 = cmd;
      patbuf0.ReplaceAll(replacement, "~");
      delete [] cmd;
      return kFALSE;
   }
   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Expand a pathname getting rid of special shell characaters like ~.$, etc.
/// User must delete returned string.

char *TWinNTSystem::ExpandPathName(const char *path)
{
   char newpath[MAX_PATH];
   if (IsShortcut(path)) {
      if (!ResolveShortCut(path, newpath, MAX_PATH))
         strlcpy(newpath, path, MAX_PATH);
   }
   else
      strlcpy(newpath, path, MAX_PATH);
   TString patbuf = newpath;
   if (ExpandPathName(patbuf)) return 0;

   return StrDup(patbuf.Data());
}

////////////////////////////////////////////////////////////////////////////////
/// Set the file permission bits. Returns -1 in case or error, 0 otherwise.
/// On windows mode can only be a combination of "user read" (0400),
/// "user write" (0200) or "user read | user write" (0600). Any other value
/// for mode are ignored.

int TWinNTSystem::Chmod(const char *file, UInt_t mode)
{
   return ::_chmod(file, mode);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the process file creation mode mask.

int TWinNTSystem::Umask(Int_t mask)
{
   return ::umask(mask);
}

////////////////////////////////////////////////////////////////////////////////
/// Set a files modification and access times. If actime = 0 it will be
/// set to the modtime. Returns 0 on success and -1 in case of error.

int TWinNTSystem::Utime(const char *file, Long_t modtime, Long_t actime)
{
   if (AccessPathName(file, kWritePermission)) {
      Error("Utime", "need write permission for %s to change utime", file);
      return -1;
   }
   if (!actime) actime = modtime;

   struct utimbuf t;
   t.actime  = (time_t)actime;
   t.modtime = (time_t)modtime;
   return ::utime(file, &t);
}

////////////////////////////////////////////////////////////////////////////////
/// Find location of file in a search path.
/// User must delete returned string. Returns 0 in case file is not found.

const char *TWinNTSystem::FindFile(const char *search, TString& infile, EAccessMode mode)
{
   // Windows cannot check on execution mode - all we can do is kReadPermission
   if (mode==kExecutePermission)
      mode=kReadPermission;

   // Expand parameters

   gSystem->ExpandPathName(infile);
   // Check whether this infile has the absolute path first
   if (IsAbsoluteFileName(infile.Data()) ) {
      if (!AccessPathName(infile.Data(), mode))
      return infile.Data();
      infile = "";
      return 0;
   }
   TString exsearch(search);
   gSystem->ExpandPathName(exsearch);

   // Need to use Windows delimiters
   Int_t lastDelim = -1;
   for(int i=0; i < exsearch.Length(); ++i) {
      switch( exsearch[i] ) {
         case ':':
            // Replace the ':' unless there are after a disk suffix (aka ;c:\mydirec...)
            if (i-lastDelim!=2) exsearch[i] = ';';
            lastDelim = i;
            break;
         case ';': lastDelim = i; break;
      }
   }

   // Check access
   struct stat finfo;
   char name[kMAXPATHLEN];
   char *lpFilePart = 0;
   if (::SearchPath(exsearch.Data(), infile.Data(), NULL, kMAXPATHLEN, name, &lpFilePart) &&
       ::access(name, mode) == 0 && stat(name, &finfo) == 0 &&
       finfo.st_mode & S_IFREG) {
      if (gEnv->GetValue("Root.ShowPath", 0)) {
         Printf("Which: %s = %s", infile, name);
      }
      infile = name;
      return infile.Data();
   }
   infile = "";
   return 0;
}

//---- Users & Groups ----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Collect local users and groups accounts information

Bool_t TWinNTSystem::InitUsersGroups()
{
   // Net* API functions allowed and OS is Windows NT/2000/XP
   if ((gEnv->GetValue("WinNT.UseNetAPI", 0)) && (::GetVersion() < 0x80000000)) {
      fActUser = -1;
      fNbGroups = fNbUsers = 0;
      HINSTANCE netapi = ::LoadLibrary("netapi32.DLL");
      if (!netapi) return kFALSE;

      p2NetApiBufferFree  = (pfn1)::GetProcAddress(netapi, "NetApiBufferFree");
      p2NetUserGetInfo  = (pfn2)::GetProcAddress(netapi, "NetUserGetInfo");
      p2NetLocalGroupGetMembers  = (pfn3)::GetProcAddress(netapi, "NetLocalGroupGetMembers");
      p2NetLocalGroupEnum = (pfn4)::GetProcAddress(netapi, "NetLocalGroupEnum");

      if (!p2NetApiBufferFree || !p2NetUserGetInfo ||
          !p2NetLocalGroupGetMembers || !p2NetLocalGroupEnum) return kFALSE;

      GetNbGroups();

      fGroups = (struct group *)calloc(fNbGroups, sizeof(struct group));
      for(int i=0;i<fNbGroups;i++) {
        fGroups[i].gr_mem = (char **)calloc(fNbUsers, sizeof (char*));
      }
      fPasswords = (struct passwd *)calloc(fNbUsers, sizeof(struct passwd));

      CollectGroups();
      ::FreeLibrary(netapi);
   }
   fGroupsInitDone = kTRUE;
   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////

Bool_t TWinNTSystem::CountMembers(const char *lpszGroupName)
{
   NET_API_STATUS NetStatus = NERR_Success;
   LPBYTE Data = NULL;
   DWORD Index = 0, ResumeHandle = 0, Total = 0;
   LOCALGROUP_MEMBERS_INFO_1 *MemberInfo;
   WCHAR wszGroupName[256];
   int iRetOp = 0;
   DWORD dwLastError = 0;

   iRetOp = MultiByteToWideChar (
            (UINT)CP_ACP,                // code page
            (DWORD)MB_PRECOMPOSED,       // character-type options
            (LPCSTR)lpszGroupName,       // address of string to map
            (int)-1,                     // number of bytes in string
            (LPWSTR)wszGroupName,        // address of wide-character buffer
            (int)sizeof(wszGroupName) ); // size of buffer

   if (iRetOp == 0) {
      dwLastError = GetLastError();
      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   // The NetLocalGroupGetMembers() API retrieves a list of the members
   // of a particular local group.
   NetStatus = p2NetLocalGroupGetMembers (NULL, wszGroupName, 1,
                            &Data, 8192, &Index, &Total, &ResumeHandle );

   if (NetStatus != NERR_Success || Data == NULL) {
      dwLastError = GetLastError();

      if (dwLastError == ERROR_ENVVAR_NOT_FOUND) {
         // This usually means that the current Group has no members.
         // We call NetLocalGroupGetMembers() again.
         // This time, we set the level to 0.
         // We do this just to confirm that the number of members in
         // this group is zero.
         NetStatus = p2NetLocalGroupGetMembers ( NULL, wszGroupName, 0,
                                  &Data, 8192, &Index, &Total, &ResumeHandle );
      }

      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   fNbUsers += Total;
   MemberInfo = (LOCALGROUP_MEMBERS_INFO_1 *)Data;

   if (Data)
      p2NetApiBufferFree(Data);

   return TRUE;
}

////////////////////////////////////////////////////////////////////////////////

Bool_t TWinNTSystem::GetNbGroups()
{
   NET_API_STATUS NetStatus = NERR_Success;
   LPBYTE Data = NULL;
   DWORD Index = 0, ResumeHandle = 0, Total = 0, i;
   LOCALGROUP_INFO_0 *GroupInfo;
   char szAnsiName[256];
   DWORD dwLastError = 0;
   int  iRetOp = 0;

   NetStatus = p2NetLocalGroupEnum(NULL, 0, &Data, 8192, &Index,
                                    &Total, &ResumeHandle );

   if (NetStatus != NERR_Success || Data == NULL) {
      dwLastError = GetLastError();
      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   fNbGroups = Total;
   GroupInfo = (LOCALGROUP_INFO_0 *)Data;
   for (i=0; i < Total; i++) {
      // Convert group name from UNICODE to ansi.
      iRetOp = WideCharToMultiByte (
               (UINT)CP_ACP,                    // code page
               (DWORD)0,                        // performance and mapping flags
               (LPCWSTR)(GroupInfo->lgrpi0_name), // address of wide-char string
               (int)-1,                        // number of characters in string
               (LPSTR)szAnsiName,            // address of buffer for new string
               (int)(sizeof(szAnsiName)),    // size of buffer
               (LPCSTR)NULL,     // address of default for unmappable characters
               (LPBOOL)NULL );     // address of flag set when default char used.

      // Now lookup all members of this group and record down their names and
      // SIDs into the output file.
      CountMembers((LPCTSTR)szAnsiName);

      GroupInfo++;
   }

   if (Data)
      p2NetApiBufferFree(Data);

   return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
///
/// Take the name and look up a SID so that we can get full
/// domain/user information
///

Long_t TWinNTSystem::LookupSID (const char *lpszAccountName, int what,
                                int &groupIdx, int &memberIdx)
{
   BOOL bRetOp = FALSE;
   PSID pSid = NULL;
   DWORD dwSidSize, dwDomainNameSize;
   BYTE bySidBuffer[MAX_SID_SIZE];
   char szDomainName[MAX_NAME_STRING];
   SID_NAME_USE sidType;
   PUCHAR puchar_SubAuthCount = NULL;
   SID_IDENTIFIER_AUTHORITY sid_identifier_authority;
   PSID_IDENTIFIER_AUTHORITY psid_identifier_authority = NULL;
   unsigned char j = 0;
   DWORD dwLastError = 0;

   pSid = (PSID)bySidBuffer;
   dwSidSize = sizeof(bySidBuffer);
   dwDomainNameSize = sizeof(szDomainName);

   bRetOp = LookupAccountName (
            (LPCTSTR)NULL,             // address of string for system name
            (LPCTSTR)lpszAccountName,  // address of string for account name
            (PSID)pSid,                // address of security identifier
            (LPDWORD)&dwSidSize,       // address of size of security identifier
            (LPTSTR)szDomainName,      // address of string for referenced domain
            (LPDWORD)&dwDomainNameSize,// address of size of domain string
            (PSID_NAME_USE)&sidType ); // address of SID-type indicator

   if (bRetOp == FALSE) {
      dwLastError = GetLastError();
      return -1;  // Unable to obtain Account SID.
   }

   bRetOp = IsValidSid((PSID)pSid);

   if (bRetOp == FALSE) {
      dwLastError = GetLastError();
      return -2;  // SID returned is invalid.
   }

   // Obtain via APIs the identifier authority value.
   psid_identifier_authority = GetSidIdentifierAuthority ((PSID)pSid);

   // Make a copy of it.
   memcpy (&sid_identifier_authority, psid_identifier_authority,
       sizeof(SID_IDENTIFIER_AUTHORITY));

   // Determine how many sub-authority values there are in the current SID.
   puchar_SubAuthCount = (PUCHAR)GetSidSubAuthorityCount((PSID)pSid);
   // Assign it to a more convenient variable.
   j = (unsigned char)(*puchar_SubAuthCount);
   // Now obtain all the sub-authority values from the current SID.
   DWORD dwSubAuth = 0;
   PDWORD pdwSubAuth = NULL;
   // Obtain the current sub-authority DWORD (referenced by a pointer)
   pdwSubAuth = (PDWORD)GetSidSubAuthority (
                (PSID)pSid,  // address of security identifier to query
                (DWORD)j-1); // index of subauthority to retrieve
   dwSubAuth = *pdwSubAuth;
   if(what == SID_MEMBER) {
       fPasswords[memberIdx].pw_uid = dwSubAuth;
       fPasswords[memberIdx].pw_gid = fGroups[groupIdx].gr_gid;
       fPasswords[memberIdx].pw_group = strdup(fGroups[groupIdx].gr_name);
   }
   else if(what == SID_GROUP) {
       fGroups[groupIdx].gr_gid = dwSubAuth;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
///

Bool_t TWinNTSystem::CollectMembers(const char *lpszGroupName, int &groupIdx,
                                    int &memberIdx)
{

   NET_API_STATUS NetStatus = NERR_Success;
   LPBYTE Data = NULL;
   DWORD Index = 0, ResumeHandle = 0, Total = 0, i;
   LOCALGROUP_MEMBERS_INFO_1 *MemberInfo;
   char szAnsiMemberName[256];
   char szFullMemberName[256];
   char szMemberHomeDir[256];
   WCHAR wszGroupName[256];
   int iRetOp = 0;
   char  act_name[256];
   DWORD length = sizeof (act_name);
   DWORD dwLastError = 0;
   LPUSER_INFO_11  pUI11Buf = NULL;
   NET_API_STATUS  nStatus;

   iRetOp = MultiByteToWideChar (
            (UINT)CP_ACP,                // code page
            (DWORD)MB_PRECOMPOSED,       // character-type options
            (LPCSTR)lpszGroupName,       // address of string to map
            (int)-1,                     // number of bytes in string
            (LPWSTR)wszGroupName,        // address of wide-character buffer
            (int)sizeof(wszGroupName) ); // size of buffer

   if (iRetOp == 0) {
      dwLastError = GetLastError();
      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   GetUserName (act_name, &length);

   // The NetLocalGroupGetMembers() API retrieves a list of the members
   // of a particular local group.
   NetStatus = p2NetLocalGroupGetMembers (NULL, wszGroupName, 1,
                            &Data, 8192, &Index, &Total, &ResumeHandle );

   if (NetStatus != NERR_Success || Data == NULL) {
      dwLastError = GetLastError();

      if (dwLastError == ERROR_ENVVAR_NOT_FOUND) {
         // This usually means that the current Group has no members.
         // We call NetLocalGroupGetMembers() again.
         // This time, we set the level to 0.
         // We do this just to confirm that the number of members in
         // this group is zero.
         NetStatus = p2NetLocalGroupGetMembers ( NULL, wszGroupName, 0,
                                  &Data, 8192, &Index, &Total, &ResumeHandle );
      }

      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   MemberInfo = (LOCALGROUP_MEMBERS_INFO_1 *)Data;
   for (i=0; i < Total; i++) {
      iRetOp = WideCharToMultiByte (
               (UINT)CP_ACP,                     // code page
               (DWORD)0,                         // performance and mapping flags
               (LPCWSTR)(MemberInfo->lgrmi1_name), // address of wide-char string
               (int)-1,                         // number of characters in string
               (LPSTR)szAnsiMemberName,       // address of buffer for new string
               (int)(sizeof(szAnsiMemberName)), // size of buffer
               (LPCSTR)NULL,      // address of default for unmappable characters
               (LPBOOL)NULL );      // address of flag set when default char used.

      if (iRetOp == 0) {
         dwLastError = GetLastError();
      }

      fPasswords[memberIdx].pw_name = strdup(szAnsiMemberName);
      fPasswords[memberIdx].pw_passwd = strdup("");
      fGroups[groupIdx].gr_mem[i] = strdup(szAnsiMemberName);

      if(fActUser == -1 && !stricmp(fPasswords[memberIdx].pw_name,act_name))
                      fActUser = memberIdx;


      TCHAR szUserName[255]=TEXT("");
      MultiByteToWideChar(CP_ACP, 0, szAnsiMemberName, -1, (LPWSTR)szUserName, 255);
      //
      // Call the NetUserGetInfo function; specify level 10.
      //
      nStatus = p2NetUserGetInfo(NULL, (LPCWSTR)szUserName, 11, (LPBYTE *)&pUI11Buf);
      //
      // If the call succeeds, print the user information.
      //
      if (nStatus == NERR_Success) {
         if (pUI11Buf != NULL) {
            wsprintf(szFullMemberName,"%S",pUI11Buf->usri11_full_name);
            fPasswords[memberIdx].pw_gecos = strdup(szFullMemberName);
            wsprintf(szMemberHomeDir,"%S",pUI11Buf->usri11_home_dir);
            fPasswords[memberIdx].pw_dir = strdup(szMemberHomeDir);
         }
      }
      if((fPasswords[memberIdx].pw_gecos == NULL) || (strlen(fPasswords[memberIdx].pw_gecos) == 0))
         fPasswords[memberIdx].pw_gecos = strdup(fPasswords[memberIdx].pw_name);
      if((fPasswords[memberIdx].pw_dir == NULL) || (strlen(fPasswords[memberIdx].pw_dir) == 0))
         fPasswords[memberIdx].pw_dir = strdup("c:\\");
      //
      // Free the allocated memory.
      //
      if (pUI11Buf != NULL) {
         p2NetApiBufferFree(pUI11Buf);
         pUI11Buf = NULL;
      }

      /* Ensure SHELL is defined. */
      if (getenv("SHELL") == NULL)
         putenv ((GetVersion () & 0x80000000) ? "SHELL=command" : "SHELL=cmd");

      /* Set dir and shell from environment variables. */
      fPasswords[memberIdx].pw_shell = getenv("SHELL");

      // Find out the SID of the Member.
      LookupSID ((LPCTSTR)szAnsiMemberName, SID_MEMBER, groupIdx, memberIdx);
      memberIdx++;
      MemberInfo++;
   }
   if(fActUser == -1)  fActUser = 0;

   if (Data)
      p2NetApiBufferFree(Data);

   return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
///

Bool_t TWinNTSystem::CollectGroups()
{
   NET_API_STATUS NetStatus = NERR_Success;
   LPBYTE Data = NULL;
   DWORD Index = 0, ResumeHandle = 0, Total = 0, i;
   LOCALGROUP_INFO_0 *GroupInfo;
   char szAnsiName[256];
   DWORD dwLastError = 0;
   int  iRetOp = 0, iGroupIdx = 0, iMemberIdx = 0;

   NetStatus = p2NetLocalGroupEnum(NULL, 0, &Data, 8192, &Index,
                                    &Total, &ResumeHandle );

   if (NetStatus != NERR_Success || Data == NULL) {
      dwLastError = GetLastError();
      if (Data)
         p2NetApiBufferFree(Data);
      return FALSE;
   }

   GroupInfo = (LOCALGROUP_INFO_0 *)Data;
   for (i=0; i < Total; i++) {
      // Convert group name from UNICODE to ansi.
      iRetOp = WideCharToMultiByte (
               (UINT)CP_ACP,                    // code page
               (DWORD)0,                        // performance and mapping flags
               (LPCWSTR)(GroupInfo->lgrpi0_name), // address of wide-char string
               (int)-1,                        // number of characters in string
               (LPSTR)szAnsiName,            // address of buffer for new string
               (int)(sizeof(szAnsiName)),    // size of buffer
               (LPCSTR)NULL,     // address of default for unmappable characters
               (LPBOOL)NULL );     // address of flag set when default char used.

      fGroups[iGroupIdx].gr_name = strdup(szAnsiName);
      fGroups[iGroupIdx].gr_passwd = strdup("");

      // Find out the SID of the Group.
      LookupSID ((LPCTSTR)szAnsiName, SID_GROUP, iGroupIdx, iMemberIdx);
      // Now lookup all members of this group and record down their names and
      // SIDs into the output file.
      CollectMembers((LPCTSTR)szAnsiName, iGroupIdx, iMemberIdx);

      iGroupIdx++;
      GroupInfo++;
   }

   if (Data)
      p2NetApiBufferFree(Data);

   return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the user's id. If user = 0, returns current user's id.

Int_t TWinNTSystem::GetUid(const char *user)
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      int   uid;
      char  name[256];
      DWORD length = sizeof (name);
      if (::GetUserName (name, &length)) {
         if (stricmp ("administrator", name) == 0)
            uid = 0;
         else
            uid = 123;
      }
      else {
         uid = 123;
      }
      return uid;
   }
   if (!user || !user[0])
      return fPasswords[fActUser].pw_uid;
   else {
      struct passwd *pwd = 0;
      for(int i=0;i<fNbUsers;i++) {
         if (!stricmp (user, fPasswords[i].pw_name)) {
            pwd = &fPasswords[i];
            break;
         }
      }
      if (pwd)
         return pwd->pw_uid;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the effective user id. The effective id corresponds to the
/// set id bit on the file being executed.

Int_t TWinNTSystem::GetEffectiveUid()
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      int   uid;
      char  name[256];
      DWORD length = sizeof (name);
      if (::GetUserName (name, &length)) {
         if (stricmp ("administrator", name) == 0)
            uid = 0;
         else
            uid = 123;
      }
      else {
         uid = 123;
      }
      return uid;
   }
   return fPasswords[fActUser].pw_uid;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the group's id. If group = 0, returns current user's group.

Int_t TWinNTSystem::GetGid(const char *group)
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      int   gid;
      char  name[256];
      DWORD length = sizeof (name);
      if (::GetUserName (name, &length)) {
         if (stricmp ("administrator", name) == 0)
            gid = 0;
         else
            gid = 123;
      }
      else {
         gid = 123;
      }
      return gid;
   }
   if (!group || !group[0])
      return fPasswords[fActUser].pw_gid;
   else {
      struct group *grp = 0;
      for(int i=0;i<fNbGroups;i++) {
         if (!stricmp (group, fGroups[i].gr_name)) {
            grp = &fGroups[i];
            break;
         }
      }
      if (grp)
         return grp->gr_gid;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the effective group id. The effective group id corresponds
/// to the set id bit on the file being executed.

Int_t TWinNTSystem::GetEffectiveGid()
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      int   gid;
      char  name[256];
      DWORD length = sizeof (name);
      if (::GetUserName (name, &length)) {
         if (stricmp ("administrator", name) == 0)
            gid = 0;
         else
            gid = 123;
      }
      else {
         gid = 123;
      }
      return gid;
   }
   return fPasswords[fActUser].pw_gid;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all user info in the UserGroup_t structure. The returned
/// structure must be deleted by the user. In case of error 0 is returned.

UserGroup_t *TWinNTSystem::GetUserInfo(Int_t uid)
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      char  name[256];
      DWORD length = sizeof (name);
      UserGroup_t *ug = new UserGroup_t;
      if (::GetUserName (name, &length)) {
         ug->fUser = name;
         if (stricmp ("administrator", name) == 0) {
            ug->fUid = 0;
            ug->fGroup = "administrators";
         }
         else {
            ug->fUid = 123;
            ug->fGroup = "users";
         }
         ug->fGid = ug->fUid;
      }
      else {
         ug->fUser = "unknown";
         ug->fGroup = "unknown";
         ug->fUid = ug->fGid = 123;
      }
      ug->fPasswd = "";
      ug->fRealName = ug->fUser;
      ug->fShell = "command";
      return ug;
   }
   struct passwd *pwd = 0;
   if (uid == 0)
      pwd = &fPasswords[fActUser];
   else {
      for (int i = 0; i < fNbUsers; i++) {
         if (uid == fPasswords[i].pw_uid) {
            pwd = &fPasswords[i];
            break;
         }
      }
   }
   if (pwd) {
      UserGroup_t *ug = new UserGroup_t;
      ug->fUid      = pwd->pw_uid;
      ug->fGid      = pwd->pw_gid;
      ug->fUser     = pwd->pw_name;
      ug->fPasswd   = pwd->pw_passwd;
      ug->fRealName = pwd->pw_gecos;
      ug->fShell    = pwd->pw_shell;
      ug->fGroup    = pwd->pw_group;
      return ug;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all user info in the UserGroup_t structure. If user = 0, returns
/// current user's id info. The returned structure must be deleted by the
/// user. In case of error 0 is returned.

UserGroup_t *TWinNTSystem::GetUserInfo(const char *user)
{
   return GetUserInfo(GetUid(user));
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all group info in the UserGroup_t structure. The only active
/// fields in the UserGroup_t structure for this call are:
///    fGid and fGroup
/// The returned structure must be deleted by the user. In case of
/// error 0 is returned.

UserGroup_t *TWinNTSystem::GetGroupInfo(Int_t gid)
{
   if(!fGroupsInitDone)
      InitUsersGroups();

   // Net* API functions not allowed or OS not Windows NT/2000/XP
   if ((!gEnv->GetValue("WinNT.UseNetAPI", 0)) || (::GetVersion() >= 0x80000000)) {
      char  name[256];
      DWORD length = sizeof (name);
      UserGroup_t *gr = new UserGroup_t;
      if (::GetUserName (name, &length)) {
         if (stricmp ("administrator", name) == 0) {
            gr->fGroup = "administrators";
            gr->fGid = 0;
         }
         else {
            gr->fGroup = "users";
            gr->fGid = 123;
         }
      }
      else {
         gr->fGroup = "unknown";
         gr->fGid = 123;
      }
      gr->fUid = 0;
      return gr;
   }
   struct group *grp = 0;
   for(int i=0;i<fNbGroups;i++) {
      if (gid == fGroups[i].gr_gid) {
         grp = &fGroups[i];
         break;
      }
   }
   if (grp) {
      UserGroup_t *gr = new UserGroup_t;
      gr->fUid   = 0;
      gr->fGid   = grp->gr_gid;
      gr->fGroup = grp->gr_name;
      return gr;
   }
   return 0;

}

////////////////////////////////////////////////////////////////////////////////
/// Returns all group info in the UserGroup_t structure. The only active
/// fields in the UserGroup_t structure for this call are:
///    fGid and fGroup
/// If group = 0, returns current user's group. The returned structure
/// must be deleted by the user. In case of error 0 is returned.

UserGroup_t *TWinNTSystem::GetGroupInfo(const char *group)
{
   return GetGroupInfo(GetGid(group));
}

//---- environment manipulation ------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Set environment variable.

void TWinNTSystem::Setenv(const char *name, const char *value)
{
   ::_putenv(TString::Format("%s=%s", name, value));
}

////////////////////////////////////////////////////////////////////////////////
/// Get environment variable.

const char *TWinNTSystem::Getenv(const char *name)
{
   const char *env = ::getenv(name);
   if (!env) {
      if (::_stricmp(name,"home") == 0 ) {
        env = HomeDirectory();
      } else if (::_stricmp(name, "rootsys") == 0 ) {
        env = gRootDir;
      }
   }
   return env;
}

//---- Processes ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Execute a command.

int TWinNTSystem::Exec(const char *shellcmd)
{
   return ::system(shellcmd);
}

////////////////////////////////////////////////////////////////////////////////
/// Open a pipe.

FILE *TWinNTSystem::OpenPipe(const char *command, const char *mode)
{
  return ::_popen(command, mode);
}

////////////////////////////////////////////////////////////////////////////////
/// Close the pipe.

int TWinNTSystem::ClosePipe(FILE *pipe)
{
  return ::_pclose(pipe);
}

////////////////////////////////////////////////////////////////////////////////
/// Get process id.

int TWinNTSystem::GetPid()
{
   return ::getpid();
}

////////////////////////////////////////////////////////////////////////////////
/// Exit the application.

void TWinNTSystem::Exit(int code, Bool_t mode)
{
   // Insures that the files and sockets are closed before any library is unloaded
   // and before emptying CINT.
   // FIXME: Unify with TROOT::ShutDown.
   if (gROOT) {
      gROOT->CloseFiles();
      gROOT->EndOfProcessCleanups();
   }
   if (gInterpreter) {
      gInterpreter->ShutDown();
   }

   if (mode) {
      ::exit(code);
   } else {
      ::_exit(code);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Abort the application.

void TWinNTSystem::Abort(int)
{
   ::abort();
}

//---- Standard output redirection ---------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Redirect standard output (stdout, stderr) to the specified file.
/// If the file argument is 0 the output is set again to stderr, stdout.
/// The second argument specifies whether the output should be added to the
/// file ("a", default) or the file be truncated before ("w").
/// This function saves internally the current state into a static structure.
/// The call can be made reentrant by specifying the opaque structure pointed
/// by 'h', which is filled with the relevant information. The handle 'h'
/// obtained on the first call must then be used in any subsequent call,
/// included ShowOutput, to display the redirected output.
/// Returns 0 on success, -1 in case of error.

Int_t TWinNTSystem::RedirectOutput(const char *file, const char *mode,
                                   RedirectHandle_t *h)
{
   FILE *fout, *ferr;
   static int fd1=0, fd2=0;
   static fpos_t pos1=0, pos2=0;
   // Instance to be used if the caller does not passes 'h'
   static RedirectHandle_t loch;
   Int_t rc = 0;

   // Which handle to use ?
   RedirectHandle_t *xh = (h) ? h : &loch;

   if (file) {
      // Make sure mode makes sense; default "a"
      const char *m = (mode[0] == 'a' || mode[0] == 'w') ? mode : "a";

      // Current file size
      xh->fReadOffSet = 0;
      if (m[0] == 'a') {
         // If the file exists, save the current size
         FileStat_t st;
         if (!gSystem->GetPathInfo(file, st))
            xh->fReadOffSet = (st.fSize > 0) ? st.fSize : xh->fReadOffSet;
      }
      xh->fFile = file;

      fflush(stdout);
      fgetpos(stdout, &pos1);
      fd1 = _dup(fileno(stdout));
      // redirect stdout & stderr
      if ((fout = freopen(file, m, stdout)) == 0) {
         SysError("RedirectOutput", "could not freopen stdout");
         if (fd1 > 0) {
            _dup2(fd1, fileno(stdout));
            close(fd1);
         }
         clearerr(stdout);
         fsetpos(stdout, &pos1);
         fd1 = fd2 = 0;
         return -1;
      }
      fflush(stderr);
      fgetpos(stderr, &pos2);
      fd2 = _dup(fileno(stderr));
      if ((ferr = freopen(file, m, stderr)) == 0) {
         SysError("RedirectOutput", "could not freopen stderr");
         if (fd1 > 0) {
            _dup2(fd1, fileno(stdout));
            close(fd1);
         }
         clearerr(stdout);
         fsetpos(stdout, &pos1);
         if (fd2 > 0) {
            _dup2(fd2, fileno(stderr));
            close(fd2);
         }
         clearerr(stderr);
         fsetpos(stderr, &pos2);
         fd1 = fd2 = 0;
         return -1;
      }
      if (m[0] == 'a') {
         fseek(fout, 0, SEEK_END);
         fseek(ferr, 0, SEEK_END);
      }
   } else {
      // Restore stdout & stderr
      fflush(stdout);
      if (fd1) {
         if (fd1 > 0) {
            if (_dup2(fd1, fileno(stdout))) {
               SysError("RedirectOutput", "could not restore stdout");
               rc = -1;
            }
            close(fd1);
         }
         clearerr(stdout);
         fsetpos(stdout, &pos1);
         fd1 = 0;
      }

      fflush(stderr);
      if (fd2) {
         if (fd2 > 0) {
            if (_dup2(fd2, fileno(stderr))) {
               SysError("RedirectOutput", "could not restore stderr");
               rc = -1;
            }
            close(fd2);
         }
         clearerr(stderr);
         fsetpos(stderr, &pos2);
         fd2 = 0;
      }

      // Reset the static instance, if using that
      if (xh == &loch)
         xh->Reset();
   }
   return rc;
}

//---- dynamic loading and linking ---------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Add a new directory to the dynamic path.

void TWinNTSystem::AddDynamicPath(const char *dir)
{
   if (dir) {
      TString oldpath = DynamicPath(0, kFALSE);
      oldpath.Append(";");
      oldpath.Append(dir);
      DynamicPath(oldpath);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return the dynamic path (used to find shared libraries).

const char* TWinNTSystem::GetDynamicPath()
{
   return DynamicPath(0, kFALSE);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the dynamic path to a new value.
/// If the value of 'path' is zero, the dynamic path is reset to its
/// default value.

void TWinNTSystem::SetDynamicPath(const char *path)
{
   if (!path)
      DynamicPath(0, kTRUE);
   else
      DynamicPath(path);
}

////////////////////////////////////////////////////////////////////////////////
/// Returns and updates sLib to the path of a dynamic library
///  (searches for library in the dynamic library search path).
/// If no file name extension is provided it tries .DLL.

const char *TWinNTSystem::FindDynamicLibrary(TString &sLib, Bool_t quiet)
{
   int len = sLib.Length();
   if (len > 4 && (!stricmp(sLib.Data()+len-4, ".dll"))) {
      if (gSystem->FindFile(GetDynamicPath(), sLib, kReadPermission))
         return sLib;
   } else {
      TString sLibDll(sLib);
      sLibDll += ".dll";
      if (gSystem->FindFile(GetDynamicPath(), sLibDll, kReadPermission)) {
         sLibDll.Swap(sLib);
         return sLib;
      }
   }

   if (!quiet) {
      Error("DynamicPathName",
            "%s does not exist in %s,\nor has wrong file extension (.dll)",
             sLib.Data(), GetDynamicPath());
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Load a shared library. Returns 0 on successful loading, 1 in
/// case lib was already loaded and -1 in case lib does not exist
/// or in case of error.

int TWinNTSystem::Load(const char *module, const char *entry, Bool_t system)
{
   return TSystem::Load(module, entry, system);
}

/* nonstandard extension used : zero-sized array in struct/union */
#pragma warning(push)
#pragma warning(disable:4200)
////////////////////////////////////////////////////////////////////////////////
/// Get list of shared libraries loaded at the start of the executable.
/// Returns 0 in case list cannot be obtained or in case of error.

const char *TWinNTSystem::GetLinkedLibraries()
{
   char winDrive[256];
   char winDir[256];
   char winName[256];
   char winExt[256];

   if (!gApplication) return 0;

   static Bool_t once = kFALSE;
   static TString linkedLibs;

   if (!linkedLibs.IsNull())
      return linkedLibs;

   if (once)
      return 0;

   char *exe = gSystem->Which(Getenv("PATH"), "python", kExecutePermission);
   if (!exe) {
      once = kTRUE;
      return 0;
   }

   HANDLE hFile, hMapping;
   void *basepointer;

   if((hFile = CreateFile(exe,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0))==INVALID_HANDLE_VALUE) {
      delete [] exe;
      return 0;
   }
   if(!(hMapping = CreateFileMapping(hFile,0,PAGE_READONLY|SEC_COMMIT,0,0,0))) {
      CloseHandle(hFile);
      delete [] exe;
      return 0;
   }
   if(!(basepointer = MapViewOfFile(hMapping,FILE_MAP_READ,0,0,0))) {
      CloseHandle(hMapping);
      CloseHandle(hFile);
      delete [] exe;
      return 0;
   }

   int sect;
   IMAGE_DOS_HEADER *dos_head = (IMAGE_DOS_HEADER *)basepointer;
   struct header {
      DWORD signature;
      IMAGE_FILE_HEADER _head;
      IMAGE_OPTIONAL_HEADER opt_head;
      IMAGE_SECTION_HEADER section_header[];  // actual number in NumberOfSections
   };
   struct header *pheader;
   const IMAGE_SECTION_HEADER * section_header;

   if(dos_head->e_magic!='ZM') {
      delete [] exe;
      return 0;
   }  // verify DOS-EXE-Header
   // after end of DOS-EXE-Header: offset to PE-Header
   pheader = (struct header *)((char*)dos_head + dos_head->e_lfanew);

   if(IsBadReadPtr(pheader,sizeof(struct header))) { // start of PE-Header
      delete [] exe;
      return 0;
   }
   if(pheader->signature!=IMAGE_NT_SIGNATURE) {      // verify PE format
      switch((unsigned short)pheader->signature) {
         case IMAGE_DOS_SIGNATURE:
            delete [] exe;
            return 0;
         case IMAGE_OS2_SIGNATURE:
            delete [] exe;
            return 0;
         case IMAGE_OS2_SIGNATURE_LE:
            delete [] exe;
            return 0;
         default: // unknown signature
            delete [] exe;
            return 0;
      }
   }
#define isin(address,start,length) ((address)>=(start) && (address)<(start)+(length))
   TString odump;
   // walk through sections
   for(sect=0,section_header=pheader->section_header;
       sect<pheader->_head.NumberOfSections;sect++,section_header++) {
      int directory;
      const void * const section_data =
            (char*)basepointer + section_header->PointerToRawData;
      for(directory=0;directory<IMAGE_NUMBEROF_DIRECTORY_ENTRIES;directory++) {
         if(isin(pheader->opt_head.DataDirectory[directory].VirtualAddress,
                 section_header->VirtualAddress,
                 section_header->SizeOfRawData)) {
            const IMAGE_IMPORT_DESCRIPTOR *stuff_start =
                 (IMAGE_IMPORT_DESCRIPTOR *)((char*)section_data +
                 (pheader->opt_head.DataDirectory[directory].VirtualAddress -
                  section_header->VirtualAddress));
            // (virtual address of stuff - virtual address of section) =
            // offset of stuff in section
            const unsigned stuff_length =
                  pheader->opt_head.DataDirectory[directory].Size;
            if(directory == IMAGE_DIRECTORY_ENTRY_IMPORT) {
               while(!IsBadReadPtr(stuff_start,sizeof(*stuff_start)) &&
                      stuff_start->Name) {
                  TString dll = (char*)section_data +
                               ((DWORD)(stuff_start->Name)) -
                                section_header->VirtualAddress;
                  if (dll.EndsWith(".dll")) {
                     char *dllPath = DynamicPathName(dll, kTRUE);
                     if (dllPath) {
                        char *winPath = getenv("windir");
                        _splitpath(winPath,winDrive,winDir,winName,winExt);
                        if(!strstr(dllPath, winDir)) {
                           if (!linkedLibs.IsNull())
                              linkedLibs += " ";
                           linkedLibs += dllPath;
                        }
                     }
                     delete [] dllPath;
                  }
                  stuff_start++;
               }
            }
         }
      }
   }

   UnmapViewOfFile(basepointer);
   CloseHandle(hMapping);
   CloseHandle(hFile);

   delete [] exe;

   once = kTRUE;

   if (linkedLibs.IsNull())
      return 0;

   return linkedLibs;
}
#pragma warning(pop)

////////////////////////////////////////////////////////////////////////////////
/// Return a space separated list of loaded shared libraries.
/// This list is of a format suitable for a linker, i.e it may contain
/// -Lpathname and/or -lNameOfLib.
/// Option can be any of:
///   S: shared libraries loaded at the start of the executable, because
///      they were specified on the link line.
///   D: shared libraries dynamically loaded after the start of the program.
///   L: list the .LIB rather than the .DLL (this is intended for linking)
///      [This options is not the default]

const char *TWinNTSystem::GetLibraries(const char *regexp, const char *options,
                                       Bool_t isRegexp)
{
   TString libs(TSystem::GetLibraries(regexp, options, isRegexp));
   TString ntlibs;
   TString opt = options;

   if ( (opt.First('L')!=kNPOS) ) {
      TRegexp separator("[^ \\t\\s]+");
      TRegexp user_dll("\\.dll$");
      TRegexp user_lib("\\.lib$");
      FileStat_t sbuf;
      TString s;
      Ssiz_t start, index, end;
      start = index = end = 0;

      while ((start < libs.Length()) && (index != kNPOS)) {
         index = libs.Index(separator, &end, start);
         if (index >= 0) {
            // Change .dll into .lib and remove the
            // path info if it not accessible.
            s = libs(index, end);
            s.ToLower();
            if ((s.Index("c:/windows/") != kNPOS) ||
                (s.Index("python") != kNPOS)) {
               start += end+1;
               continue;
            }
            if (s.Index(user_dll) != kNPOS) {
               s.ReplaceAll(".dll",".lib");
               if ( GetPathInfo( s, sbuf ) != 0 ) {
                  s.Replace( 0, s.Last('/')+1, 0, 0);
                  s.Replace( 0, s.Last('\\')+1, 0, 0);
               }
            } else if (s.Index(user_lib) != kNPOS) {
               if ( GetPathInfo( s, sbuf ) != 0 ) {
                  s.Replace( 0, s.Last('/')+1, 0, 0);
                  s.Replace( 0, s.Last('\\')+1, 0, 0);
               }
            }
            if (!ntlibs.IsNull()) ntlibs.Append(" ");
            if ((s.Index("python") == kNPOS) && (s.Index("cppyy") == kNPOS) &&
                (s.Index("vcruntime") == kNPOS) && (s.Index(".pyd") == kNPOS))
              ntlibs.Append(s);
         }
         start += end+1;
      }
   } else {
      ntlibs = libs;
   }

   fListLibs = ntlibs;
   fListLibs.ReplaceAll("/","\\");
   return fListLibs;
}

//---- Time & Date -------------------------------------------------------------
const Double_t gTicks = 1.0e-7;
////////////////////////////////////////////////////////////////////////////////
///

Double_t TWinNTSystem::GetRealTime()
{
   union {
      FILETIME ftFileTime;
      __int64  ftInt64;
   } ftRealTime; // time the process has spent in kernel mode

   ::GetSystemTimeAsFileTime(&ftRealTime.ftFileTime);
   return (Double_t)ftRealTime.ftInt64 * gTicks;
}

////////////////////////////////////////////////////////////////////////////////
///

Double_t TWinNTSystem::GetCPUTime()
{
   OSVERSIONINFO OsVersionInfo;

//*-*         Value                      Platform
//*-*  ----------------------------------------------------
//*-*  VER_PLATFORM_WIN32s              Win32s on Windows 3.1
//*-*  VER_PLATFORM_WIN32_WINDOWS       Win32 on Windows 95
//*-*  VER_PLATFORM_WIN32_NT            Windows NT
//*-*

   OsVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
   GetVersionEx(&OsVersionInfo);
   if (OsVersionInfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
      DWORD       ret;
      FILETIME    ftCreate,       // when the process was created
                  ftExit;         // when the process exited

      union {
         FILETIME ftFileTime;
         __int64  ftInt64;
      } ftKernel; // time the process has spent in kernel mode

      union {
         FILETIME ftFileTime;
         __int64  ftInt64;
      } ftUser;   // time the process has spent in user mode

      HANDLE hThread = GetCurrentThread();
      ret = GetThreadTimes (hThread, &ftCreate, &ftExit,
                                     &ftKernel.ftFileTime,
                                     &ftUser.ftFileTime);
      if (ret != TRUE){
         ret = ::GetLastError();
         ::CppyyLegacy::Error("GetCPUTime", " Error on GetProcessTimes 0x%lx", (int)ret);
      }

      // Process times are returned in a 64-bit structure, as the number of
      // 100 nanosecond ticks since 1 January 1601.  User mode and kernel mode
      // times for this process are in separate 64-bit structures.
      // To convert to floating point seconds, we will:
      //          Convert sum of high 32-bit quantities to 64-bit int

       return (Double_t) (ftKernel.ftInt64 + ftUser.ftInt64) * gTicks;
   } else {
      return GetRealTime();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Get current time in milliseconds since 0:00 Jan 1 1995.

TTime TWinNTSystem::Now()
{
   static time_t jan95 = 0;
   if (!jan95) {
      struct tm tp;
      tp.tm_year  = 95;
      tp.tm_mon   = 0;
      tp.tm_mday  = 1;
      tp.tm_hour  = 0;
      tp.tm_min   = 0;
      tp.tm_sec   = 0;
      tp.tm_isdst = -1;

      jan95 = mktime(&tp);
      if ((int)jan95 == -1) {
         ::CppyyLegacy::SysError("TWinNTSystem::Now", "error converting 950001 0:00 to time_t");
         return 0;
      }
   }

   _timeb now;
   _ftime(&now);
   return TTime((now.time-(Long_t)jan95)*1000 + now.millitm);
}

////////////////////////////////////////////////////////////////////////////////
/// Get Internet Protocol (IP) address of host.

TInetAddress TWinNTSystem::GetHostByName(const char *hostname)
{
   struct hostent *host_ptr;
   const char     *host;
   int             type;
   UInt_t          addr;    // good for 4 byte addresses

   if ((addr = ::inet_addr(hostname)) != INADDR_NONE) {
      type = AF_INET;
      if ((host_ptr = ::gethostbyaddr((const char *)&addr,
                                      sizeof(addr), AF_INET))) {
         host = host_ptr->h_name;
         TInetAddress a(host, ntohl(addr), type);
         UInt_t addr2;
         Int_t  i;
         for (i = 1; host_ptr->h_addr_list[i]; i++) {
            memcpy(&addr2, host_ptr->h_addr_list[i], host_ptr->h_length);
            a.AddAddress(ntohl(addr2));
         }
         for (i = 0; host_ptr->h_aliases[i]; i++)
            a.AddAlias(host_ptr->h_aliases[i]);
         return a;
      } else {
         host = "UnNamedHost";
      }
   } else if ((host_ptr = ::gethostbyname(hostname))) {
      // Check the address type for an internet host
      if (host_ptr->h_addrtype != AF_INET) {
         Error("GetHostByName", "%s is not an internet host\n", hostname);
         return TInetAddress();
      }
      memcpy(&addr, host_ptr->h_addr, host_ptr->h_length);
      host = host_ptr->h_name;
      type = host_ptr->h_addrtype;
      TInetAddress a(host, ntohl(addr), type);
      UInt_t addr2;
      Int_t  i;
      for (i = 1; host_ptr->h_addr_list[i]; i++) {
         memcpy(&addr2, host_ptr->h_addr_list[i], host_ptr->h_length);
         a.AddAddress(ntohl(addr2));
      }
      for (i = 0; host_ptr->h_aliases[i]; i++)
         a.AddAlias(host_ptr->h_aliases[i]);
      return a;
   } else {
      if (gDebug > 0) Error("GetHostByName", "unknown host %s", hostname);
      return TInetAddress(hostname, 0, -1);
   }

   return TInetAddress(host, ::ntohl(addr), type);
}

} // namespace CppyyLegacy
