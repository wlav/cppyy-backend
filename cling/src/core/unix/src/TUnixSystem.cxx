// @(#)root/unix:$Id: 887c618d89c4ed436e4034fc133f468fecad651b $
// Author: Fons Rademakers   15/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TUnixSystem                                                          //
//                                                                      //
// Class providing an interface to the UNIX Operating System.           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "RConfigure.h"
#include <ROOT/RConfig.hxx>
#include <ROOT/FoundationUtils.hxx>
#include "TUnixSystem.h"
#include "TROOT.h"
#include "TError.h"
#include "TOrdCollection.h"
#include "TRegexp.h"
#include "TException.h"
#include "Demangle.h"
#include "TEnv.h"
#include "Getline.h"
#include "TInterpreter.h"
#include "TApplication.h"
#include "TObjString.h"
#include "Riostream.h"
#include "TVirtualMutex.h"
#include "TObjArray.h"
#include "TTime.h"
#include <map>
#include <algorithm>
#include <atomic>

//#define G__OLDEXPAND

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#if defined(R__SUN) || defined(R__AIX) || \
    defined(R__LINUX) || defined(R__SOLARIS) || \
    defined(R__FBSD) || defined(R__OBSD) || \
    defined(R__MACOSX) || defined(R__HURD)
#define HAS_DIRENT
#endif
#ifdef HAS_DIRENT
#   include <dirent.h>
#else
#   include <sys/dir.h>
#endif
#if defined(ULTRIX) || defined(R__SUN)
#   include <sgtty.h>
#endif
#if defined(R__AIX) || defined(R__LINUX) || \
    defined(R__FBSD) || defined(R__OBSD) || \
    defined(R__LYNXOS) || defined(R__MACOSX) || defined(R__HURD)
#   include <sys/ioctl.h>
#endif
#if defined(R__AIX) || defined(R__SOLARIS)
#   include <sys/select.h>
#endif
#if defined(R__MACOSX)
#   include <mach-o/dyld.h>
#   include <sys/mount.h>
   extern "C" int statfs(const char *file, struct statfs *buffer);
#elif defined(R__LINUX) || defined(R__HURD)
#   include <sys/vfs.h>
#elif defined(R__FBSD) || defined(R__OBSD)
#   include <sys/param.h>
#   include <sys/mount.h>
#else
#   include <sys/statfs.h>
#endif

#include <utime.h>
#include <syslog.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/param.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#if defined(R__AIX)
#   define _XOPEN_EXTENDED_SOURCE
#   include <arpa/inet.h>
#   undef _XOPEN_EXTENDED_SOURCE
#   if !defined(_AIX41) && !defined(_AIX43)
    // AIX 3.2 doesn't have it
#   define HASNOT_INETATON
#   endif
#else
#   include <arpa/inet.h>
#endif
#include <sys/un.h>
#include <netdb.h>
#include <fcntl.h>
#if defined(R__SOLARIS)
#   include <sys/systeminfo.h>
#   include <sys/filio.h>
#   include <sys/sockio.h>
#   define HASNOT_INETATON
#   ifndef INADDR_NONE
#      define INADDR_NONE (UInt_t)-1
#   endif
#endif

#if defined(R__SOLARIS)
#   define HAVE_UTMPX_H
#   define UTMP_NO_ADDR
#endif

#if defined(MAC_OS_X_VERSION_10_5)
#   define HAVE_UTMPX_H
#   define UTMP_NO_ADDR
#endif

#if defined(R__FBSD)
#   include <sys/param.h>
#   if __FreeBSD_version >= 900007
#      define HAVE_UTMPX_H
#   endif
#endif

#if defined(R__AIX) || defined(R__FBSD) || \
    defined(R__OBSD) || defined(R__LYNXOS) || \
    (defined(R__MACOSX) && !defined(MAC_OS_X_VERSION_10_5))
#   define UTMP_NO_ADDR
#endif

#if (defined(R__AIX) && !defined(_AIX43)) || \
    (defined(R__SUNGCC3) && !defined(__arch64__))
#   define USE_SIZE_T
#elif defined(R__GLIBC) || defined(R__FBSD) || \
      (defined(R__SUNGCC3) && defined(__arch64__)) || \
      defined(R__OBSD) || defined(MAC_OS_X_VERSION_10_4) || \
      (defined(R__AIX) && defined(_AIX43)) || \
      (defined(R__SOLARIS) && defined(_SOCKLEN_T))
#   define USE_SOCKLEN_T
#endif

#if defined(R__LYNXOS)
extern "C" {
   extern int putenv(const char *);
   extern int inet_aton(const char *, struct in_addr *);
};
#endif

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#define STRUCT_UTMP struct utmpx
#else
#include <utmp.h>
#define STRUCT_UTMP struct utmp
#endif
#if !defined(UTMP_FILE) && defined(_PATH_UTMP)      // 4.4BSD
#define UTMP_FILE _PATH_UTMP
#endif
#if defined(UTMPX_FILE)                             // Solaris, SysVr4
#undef  UTMP_FILE
#define UTMP_FILE UTMPX_FILE
#endif
#ifndef UTMP_FILE
#define UTMP_FILE "/etc/utmp"
#endif

// stack trace code
#if (defined(R__LINUX) || defined(R__HURD)) && !defined(R__WINGCC)
#   if __GLIBC__ == 2 && __GLIBC_MINOR__ >= 1
#      define HAVE_BACKTRACE_SYMBOLS_FD
#   endif
#   define HAVE_DLADDR
#endif
#if defined(R__MACOSX)
#      define HAVE_BACKTRACE_SYMBOLS_FD
#      define HAVE_DLADDR
#endif

#ifdef HAVE_BACKTRACE_SYMBOLS_FD
#   include <execinfo.h>
#endif
#ifdef HAVE_DLADDR
#   ifndef __USE_GNU
#      define __USE_GNU
#   endif
#   include <dlfcn.h>
#endif

#ifdef HAVE_BACKTRACE_SYMBOLS_FD
   // The maximum stack trace depth for systems where we request the
   // stack depth separately (currently glibc-based systems).
   static const int kMAX_BACKTRACE_DEPTH = 128;
#endif

// FPE handling includes
#if (defined(R__LINUX) && !defined(R__WINGCC))
#include <fpu_control.h>
#include <fenv.h>
#include <sys/prctl.h>    // for prctl() function used in StackTrace()
#endif

#if defined(R__MACOSX) && defined(__SSE2__)
#include <xmmintrin.h>
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && !defined(__xlC__) && \
   !defined(__i386__) && !defined(__x86_64__) && !defined(__arm__) && \
   !defined(__arm64__)
#include <fenv.h>
#include <signal.h>
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <mach/thread_status.h>

#define fegetenvd(x) asm volatile("mffs %0" : "=f" (x));
#define fesetenvd(x) asm volatile("mtfsf 255,%0" : : "f" (x));

enum {
  FE_ENABLE_INEXACT    = 0x00000008,
  FE_ENABLE_DIVBYZERO  = 0x00000010,
  FE_ENABLE_UNDERFLOW  = 0x00000020,
  FE_ENABLE_OVERFLOW   = 0x00000040,
  FE_ENABLE_INVALID    = 0x00000080,
  FE_ENABLE_ALL_EXCEPT = 0x000000F8
};
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || defined(__arm64__))
#include <fenv.h>
#endif
// End FPE handling includes
#define Printf TStringPrintf
namespace {
   // Depending on the platform the struct utmp (or utmpx) has either ut_name or ut_user
   // which are semantically equivalent. Instead of using preprocessor magic,
   // which is bothersome for cxx modules use SFINAE.

   template<typename T>
   struct ut_name {
      template<typename U = T, typename std::enable_if<std::is_member_pointer<decltype(&U::ut_name)>::value, int>::type = 0>
      static char getValue(U* ue, int) {
         return ue->ut_name[0];
      }

      template<typename U = T, typename std::enable_if<std::is_member_pointer<decltype(&U::ut_user)>::value, int>::type = 0>
      static char getValue(U* ue, long) {
         return ue->ut_user[0];
      }
   };

   static char get_ut_name(STRUCT_UTMP *ue) {
      // 0 is an integer literal forcing an overload pickup in case both ut_name and ut_user are present.
      return ut_name<STRUCT_UTMP>::getValue(ue, 0);
   }
}

namespace CppyyLegacy {

struct TUtmpContent {
   STRUCT_UTMP *fUtmpContents;
   UInt_t       fEntries; // Number of entries in utmp file.

   TUtmpContent() : fUtmpContents(0), fEntries(0) {}
   ~TUtmpContent() { free(fUtmpContents); }

   STRUCT_UTMP *SearchUtmpEntry(const char *tty)
   {
      // Look for utmp entry which is connected to terminal tty.

      STRUCT_UTMP *ue = fUtmpContents;

      UInt_t n = fEntries;
      while (n--) {
         if (get_ut_name(ue) && !strncmp(tty, ue->ut_line, sizeof(ue->ut_line)))
            return ue;
         ue++;
      }
      return 0;
   }

   int ReadUtmpFile()
   {
      // Read utmp file. Returns number of entries in utmp file.

      FILE  *utmp;
      struct stat file_stats;
      size_t n_read, size;

      fEntries = 0;

      R__LOCKGUARD2(gSystemMutex);

      utmp = fopen(UTMP_FILE, "r");
      if (!utmp)
         return 0;

      if (fstat(fileno(utmp), &file_stats) == -1) {
         fclose(utmp);
         return 0;
      }
      size = file_stats.st_size;
      if (size <= 0) {
         fclose(utmp);
         return 0;
      }

      fUtmpContents = (STRUCT_UTMP *) malloc(size);
      if (!fUtmpContents) {
         fclose(utmp);
         return 0;
      }

      n_read = fread(fUtmpContents, 1, size, utmp);
      if (!ferror(utmp)) {
         if (fclose(utmp) != EOF && n_read == size) {
            fEntries = size / sizeof(STRUCT_UTMP);
            return fEntries;
         }
      } else
         fclose(utmp);

      free(fUtmpContents);
      fUtmpContents = 0;
      return 0;
   }

};

const char *kServerPath     = "/tmp";
const char *kProtocolName   = "tcp";

//------------------- Unix TFdSet ----------------------------------------------
#ifndef HOWMANY
#   define HOWMANY(x, y)   (((x)+((y)-1))/(y))
#endif

const Int_t kNFDBITS = (sizeof(Long_t) * 8);  // 8 bits per byte
#ifdef FD_SETSIZE
const Int_t kFDSETSIZE = FD_SETSIZE;          // Linux = 1024 file descriptors
#else
const Int_t kFDSETSIZE = 256;                 // upto 256 file descriptors
#endif


class TFdSet {
private:
   ULong_t fds_bits[HOWMANY(kFDSETSIZE, kNFDBITS)];
public:
   TFdSet() { memset(fds_bits, 0, sizeof(fds_bits)); }
   TFdSet(const TFdSet &org) { memcpy(fds_bits, org.fds_bits, sizeof(org.fds_bits)); }
   TFdSet &operator=(const TFdSet &rhs) { if (this != &rhs) { memcpy(fds_bits, rhs.fds_bits, sizeof(rhs.fds_bits));} return *this; }
   void   Zero() { memset(fds_bits, 0, sizeof(fds_bits)); }
   void   Set(Int_t n)
   {
      if (n >= 0 && n < kFDSETSIZE) {
         fds_bits[n/kNFDBITS] |= (1UL << (n % kNFDBITS));
      } else {
         ::CppyyLegacy::Fatal("TFdSet::Set","fd (%d) out of range [0..%d]", n, kFDSETSIZE-1);
      }
   }
   void   Clr(Int_t n)
   {
      if (n >= 0 && n < kFDSETSIZE) {
         fds_bits[n/kNFDBITS] &= ~(1UL << (n % kNFDBITS));
      } else {
         ::CppyyLegacy::Fatal("TFdSet::Clr","fd (%d) out of range [0..%d]", n, kFDSETSIZE-1);
      }
   }
   Int_t  IsSet(Int_t n)
   {
      if (n >= 0 && n < kFDSETSIZE) {
         return (fds_bits[n/kNFDBITS] & (1UL << (n % kNFDBITS))) != 0;
      } else {
         ::CppyyLegacy::Fatal("TFdSet::IsSet","fd (%d) out of range [0..%d]", n, kFDSETSIZE-1);
         return 0;
      }
   }
   ULong_t *GetBits() { return (ULong_t *)fds_bits; }
};

////////////////////////////////////////////////////////////////////////////////
/// Unix signal handler.

static void SigHandler(ESignals sig)
{
   if (gSystem)
      ((TUnixSystem*)gSystem)->DispatchSignals(sig);
}

////////////////////////////////////////////////////////////////////////////////

static const char *GetExePath()
{
   TTHREAD_TLS_DECL(TString,exepath);
   if (exepath == "") {
#if defined(R__MACOSX)
      exepath = _dyld_get_image_name(0);
#elif defined(R__LINUX) || defined(R__SOLARIS) || defined(R__FBSD)
      char buf[kMAXPATHLEN];  // exe path name

      // get the name from the link in /proc
#if defined(R__LINUX)
      int ret = readlink("/proc/self/exe", buf, kMAXPATHLEN);
#elif defined(R__SOLARIS)
      int ret = readlink("/proc/self/path/a.out", buf, kMAXPATHLEN);
#elif defined(R__FBSD)
      int ret = readlink("/proc/curproc/file", buf, kMAXPATHLEN);
#endif
      if (ret > 0 && ret < kMAXPATHLEN) {
         buf[ret] = 0;
         exepath = buf;
      }
#else
      if (!gApplication)
         return exepath;
      TString p = gApplication->Argv(0);
      if (p.BeginsWith("/"))
         exepath = p;
      else if (p.Contains("/")) {
         exepath = gSystem->WorkingDirectory();
         exepath += "/";
         exepath += p;
      } else {
         char *exe = gSystem->Which(gSystem->Getenv("PATH"), p, kExecutePermission);
         if (exe) {
            exepath = exe;
            delete [] exe;
         }
      }
#endif
   }
   return exepath;
}

#if defined(HAVE_DLADDR) && !defined(R__MACOSX)
////////////////////////////////////////////////////////////////////////////////

static void SetRootSys()
{
#ifdef ROOTPREFIX
   if (gSystem->Getenv("ROOTIGNOREPREFIX")) {
#endif
   void *addr = (void *)SetRootSys;
   Dl_info info;
   if (dladdr(addr, &info) && info.dli_fname && info.dli_fname[0]) {
      char respath[kMAXPATHLEN];
      if (!realpath(info.dli_fname, respath)) {
         if (!gSystem->Getenv("ROOTSYS"))
            ::CppyyLegacy::SysError("TUnixSystem::SetRootSys", "error getting realpath of libCoreLegacy, please set ROOTSYS in the shell");
      } else {
         TString rs = gSystem->DirName(respath);
         gSystem->Setenv("ROOTSYS", gSystem->DirName(rs));
      }
   }
#ifdef ROOTPREFIX
   }
#endif
}
#endif

#if defined(R__MACOSX)
static TString gLinkedDylibs;

////////////////////////////////////////////////////////////////////////////////

static void DylibAdded(const struct mach_header *mh, intptr_t /* vmaddr_slide */)
{
   static int i = 0;
   static Bool_t gotFirstSo = kFALSE;
   static TString linkedDylibs;

   // to copy the local linkedDylibs to the global gLinkedDylibs call this
   // function with mh==0
   if (!mh) {
      gLinkedDylibs = linkedDylibs;
      return;
   }

   TString lib = _dyld_get_image_name(i++);

   TRegexp sovers = "libCoreLegacy\\.[0-9]+\\.*[0-9]*\\.*[0-9]*\\.so";
   TRegexp dyvers = "libCoreLegacy\\.[0-9]+\\.*[0-9]*\\.*[0-9]*\\.dylib";

#ifdef ROOTPREFIX
   if (gSystem->Getenv("ROOTIGNOREPREFIX")) {
#endif
   if (lib.EndsWith("libCoreLegacy.dylib") || lib.EndsWith("libCoreLegacy.so") ||
       lib.Index(sovers) != kNPOS    || lib.Index(dyvers) != kNPOS) {
      char respath[kMAXPATHLEN];
      if (!realpath(lib, respath)) {
         if (!gSystem->Getenv("ROOTSYS"))
            ::CppyyLegacy::SysError("TUnixSystem::DylibAdded", "error getting realpath of libCoreLegacy, please set ROOTSYS in the shell");
      } else {
         TString rs = gSystem->DirName(respath);
         gSystem->Setenv("ROOTSYS", gSystem->DirName(rs));
      }
   }
#ifdef ROOTPREFIX
   }
#endif

   // when libSystem.B.dylib is loaded we have finished loading all dylibs
   // explicitly linked against the executable. Additional dylibs
   // come when they are explicitly linked against loaded so's, currently
   // we are not interested in these
   if (lib.EndsWith("/libSystem.B.dylib"))
      gotFirstSo = kTRUE;

   // add all libs loaded before libSystem.B.dylib
   if (!gotFirstSo && (lib.EndsWith(".dylib") || lib.EndsWith(".so"))) {
      sovers = "\\.[0-9]+\\.*[0-9]*\\.so";
      Ssiz_t idx = lib.Index(sovers);
      if (idx != kNPOS) {
         lib.Remove(idx);
         lib += ".so";
      }
      dyvers = "\\.[0-9]+\\.*[0-9]*\\.dylib";
      idx = lib.Index(dyvers);
      if (idx != kNPOS) {
         lib.Remove(idx);
         lib += ".dylib";
      }
      if (!gSystem->AccessPathName(lib, kReadPermission)) {
         if (linkedDylibs.Length())
            linkedDylibs += " ";
         linkedDylibs += lib;
      }
   }
}
#endif

} // namespace CppyyLegacy

ClassImp(TUnixSystem);

namespace CppyyLegacy {

////////////////////////////////////////////////////////////////////////////////

TUnixSystem::TUnixSystem() : TSystem("Unix", "Unix System")
{ }

////////////////////////////////////////////////////////////////////////////////
/// Reset to original state.

TUnixSystem::~TUnixSystem()
{
   UnixResetSignals();

   delete fReadmask;
   delete fWritemask;
   delete fReadready;
   delete fWriteready;
   delete fSignals;
}

////////////////////////////////////////////////////////////////////////////////
/// Initialize Unix system interface.

Bool_t TUnixSystem::Init()
{
   if (TSystem::Init())
      return kTRUE;

   fReadmask   = new TFdSet;
   fWritemask  = new TFdSet;
   fReadready  = new TFdSet;
   fWriteready = new TFdSet;
   fSignals    = new TFdSet;

   //--- install default handlers
   UnixSignal(kSigChild,                 SigHandler);
   UnixSignal(kSigBus,                   SigHandler);
   UnixSignal(kSigSegmentationViolation, SigHandler);
   UnixSignal(kSigIllegalInstruction,    SigHandler);
   UnixSignal(kSigAbort,                 SigHandler);
   UnixSignal(kSigSystem,                SigHandler);
   UnixSignal(kSigAlarm,                 SigHandler);
   UnixSignal(kSigUrgent,                SigHandler);
   UnixSignal(kSigFloatingException,     SigHandler);
   UnixSignal(kSigWindowChanged,         SigHandler);
   UnixSignal(kSigUser2,                 SigHandler);

#if defined(R__MACOSX)
   // trap loading of all dylibs to register dylib name,
   // sets also ROOTSYS if built without ROOTPREFIX
   _dyld_register_func_for_add_image(DylibAdded);
#elif defined(HAVE_DLADDR)
   SetRootSys();
#endif

   // This is a fallback in case TROOT::GetRootSys() can't determine ROOTSYS
   gRootDir = FoundationUtils::GetFallbackRootSys().c_str();

   return kFALSE;
}

//---- Misc --------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Set the application name (from command line, argv[0]) and copy it in
/// gProgName. Copy the application pathname in gProgPath.
/// If name is 0 let the system set the actual executable name and path
/// (works on MacOS X and Linux).

void TUnixSystem::SetProgname(const char *name)
{
   if (gProgName)
      delete [] gProgName;
   if (gProgPath)
      delete [] gProgPath;

   if (!name || !*name) {
      name = GetExePath();
      gProgName = StrDup(BaseName(name));
      gProgPath = StrDup(DirName(name));
   } else {
      gProgName = StrDup(BaseName(name));
      char *w   = Which(Getenv("PATH"), gProgName);
      gProgPath = StrDup(DirName(w));
      delete [] w;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Set DISPLAY environment variable based on utmp entry. Only for UNIX.

void TUnixSystem::SetDisplay()
{
   if (!Getenv("DISPLAY")) {
      char *tty = ::ttyname(0);  // device user is logged in on
      if (tty) {
         tty += 5;               // remove "/dev/"

         TUtmpContent utmp;
         utmp.ReadUtmpFile();

         STRUCT_UTMP *utmp_entry = utmp.SearchUtmpEntry(tty);
         if (utmp_entry) {
            if (utmp_entry->ut_host[0]) {
               if (strchr(utmp_entry->ut_host, ':')) {
                  Setenv("DISPLAY", utmp_entry->ut_host);
                  Warning("SetDisplay", "DISPLAY not set, setting it to %s",
                          utmp_entry->ut_host);
               } else {
                  char disp[260];
                  snprintf(disp, sizeof(disp), "%s:0.0", utmp_entry->ut_host);
                  Setenv("DISPLAY", disp);
                  Warning("SetDisplay", "DISPLAY not set, setting it to %s",
                          disp);
               }
            }
#ifndef UTMP_NO_ADDR
            else if (utmp_entry->ut_addr) {

               struct sockaddr_in addr;
               addr.sin_family = AF_INET;
               addr.sin_port = 0;
               memcpy(&addr.sin_addr, &utmp_entry->ut_addr, sizeof(addr.sin_addr));
               memset(&addr.sin_zero[0], 0, sizeof(addr.sin_zero));
               struct sockaddr *sa = (struct sockaddr *) &addr;    // input

               char hbuf[NI_MAXHOST + 4];
               if (getnameinfo(sa, sizeof(struct sockaddr), hbuf, sizeof(hbuf), nullptr, 0, NI_NAMEREQD) == 0) {
                  assert( strlen(hbuf) < NI_MAXHOST );
                  strcat(hbuf, ":0.0");
                  Setenv("DISPLAY", hbuf);
                  Warning("SetDisplay", "DISPLAY not set, setting it to %s",
                          hbuf);
               }
            }
#endif
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return system error string.

const char *TUnixSystem::GetError()
{
   Int_t err = GetErrno();
   if (err == 0 && GetLastErrorString() != "")
      return GetLastErrorString();

#if defined(R__SOLARIS) || defined (R__LINUX) || defined(R__AIX) || \
    defined(R__FBSD) || defined(R__OBSD) || defined(R__HURD)
   return strerror(err);
#else
   if (err < 0 || err >= sys_nerr)
      return Form("errno out of range %d", err);
   return sys_errlist[err];
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Return the system's host name.

const char *TUnixSystem::HostName()
{
   if (fHostname == "") {
      char hn[64];
#if defined(R__SOLARIS)
      sysinfo(SI_HOSTNAME, hn, sizeof(hn));
#else
      gethostname(hn, sizeof(hn));
#endif
      fHostname = hn;
   }
   return (const char *)fHostname;
}

////////////////////////////////////////////////////////////////////////////////
/// Add a signal handler to list of system signal handlers. Only adds
/// the handler if it is not already in the list of signal handlers.

void TUnixSystem::AddSignalHandler(TSignalHandler *h)
{
   R__LOCKGUARD2(gSystemMutex);

   TSystem::AddSignalHandler(h);
   UnixSignal(h->GetSignal(), SigHandler);
}

////////////////////////////////////////////////////////////////////////////////
/// Remove a signal handler from list of signal handlers. Returns
/// the handler or 0 if the handler was not in the list of signal handlers.

TSignalHandler *TUnixSystem::RemoveSignalHandler(TSignalHandler *h)
{
   if (!h) return 0;

   R__LOCKGUARD2(gSystemMutex);

   TSignalHandler *oh = TSystem::RemoveSignalHandler(h);

   Bool_t last = kTRUE;
   TSignalHandler *hs;
   TIter next(fSignalHandler);

   while ((hs = (TSignalHandler*) next())) {
      if (hs->GetSignal() == h->GetSignal())
         last = kFALSE;
   }
   if (last)
      ResetSignal(h->GetSignal(), kTRUE);

   return oh;
}

////////////////////////////////////////////////////////////////////////////////
/// If reset is true reset the signal handler for the specified signal
/// to the default handler, else restore previous behaviour.

void TUnixSystem::ResetSignal(ESignals sig, Bool_t reset)
{
   if (reset)
      UnixResetSignal(sig);
   else
      UnixSignal(sig, SigHandler);
}

////////////////////////////////////////////////////////////////////////////////
/// Reset signals handlers to previous behaviour.

void TUnixSystem::ResetSignals()
{
   UnixResetSignals();
}

////////////////////////////////////////////////////////////////////////////////
/// If ignore is true ignore the specified signal, else restore previous
/// behaviour.

void TUnixSystem::IgnoreSignal(ESignals sig, Bool_t ignore)
{
   UnixIgnoreSignal(sig, ignore);
}

////////////////////////////////////////////////////////////////////////////////
/// When the argument is true the SIGALRM signal handler is set so that
/// interrupted syscalls will not be restarted by the kernel. This is
/// typically used in case one wants to put a timeout on an I/O operation.
/// By default interrupted syscalls will always be restarted (for all

void TUnixSystem::SigAlarmInterruptsSyscalls(Bool_t set)
{
   UnixSigAlarmInterruptsSyscalls(set);
}

////////////////////////////////////////////////////////////////////////////////
/// Return the bitmap of conditions that trigger a floating point exception.

Int_t TUnixSystem::GetFPEMask()
{
   Int_t mask = 0;

#if defined(R__LINUX) && !defined(__powerpc__)
#if defined(__GLIBC__) && (__GLIBC__>2 || __GLIBC__==2 && __GLIBC_MINOR__>=1)

#if __GLIBC_MINOR__>=3

   Int_t oldmask = fegetexcept();

#else
   fenv_t oldenv;
   fegetenv(&oldenv);
   fesetenv(&oldenv);
#if __ia64__
   Int_t oldmask = ~oldenv;
#else
   Int_t oldmask = ~oldenv.__control_word;
#endif
#endif

   if (oldmask & FE_INVALID  )   mask |= kInvalid;
   if (oldmask & FE_DIVBYZERO)   mask |= kDivByZero;
   if (oldmask & FE_OVERFLOW )   mask |= kOverflow;
   if (oldmask & FE_UNDERFLOW)   mask |= kUnderflow;
# ifdef FE_INEXACT
   if (oldmask & FE_INEXACT  )   mask |= kInexact;
# endif
#endif
#endif

#if defined(R__MACOSX) && defined(__SSE2__)
   // OS X uses the SSE unit for all FP math by default, not the x87 FP unit
   Int_t oldmask = ~_MM_GET_EXCEPTION_MASK();

   if (oldmask & _MM_MASK_INVALID  )   mask |= kInvalid;
   if (oldmask & _MM_MASK_DIV_ZERO )   mask |= kDivByZero;
   if (oldmask & _MM_MASK_OVERFLOW )   mask |= kOverflow;
   if (oldmask & _MM_MASK_UNDERFLOW)   mask |= kUnderflow;
   if (oldmask & _MM_MASK_INEXACT  )   mask |= kInexact;
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || defined(__arm64__))
   fenv_t oldenv;
   fegetenv(&oldenv);
   fesetenv(&oldenv);
#if defined(__arm__)
   Int_t oldmask = ~oldenv.__fpscr;
#elif defined(__arm64__)
   Int_t oldmask = ~oldenv.__fpcr;
#else
   Int_t oldmask = ~oldenv.__control;
#endif

   if (oldmask & FE_INVALID  )   mask |= kInvalid;
   if (oldmask & FE_DIVBYZERO)   mask |= kDivByZero;
   if (oldmask & FE_OVERFLOW )   mask |= kOverflow;
   if (oldmask & FE_UNDERFLOW)   mask |= kUnderflow;
   if (oldmask & FE_INEXACT  )   mask |= kInexact;
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && !defined(__xlC__) && \
    !defined(__i386__) && !defined(__x86_64__) && !defined(__arm__) && \
    !defined(__arm64__)
   Long64_t oldmask;
   fegetenvd(oldmask);

   if (oldmask & FE_ENABLE_INVALID  )   mask |= kInvalid;
   if (oldmask & FE_ENABLE_DIVBYZERO)   mask |= kDivByZero;
   if (oldmask & FE_ENABLE_OVERFLOW )   mask |= kOverflow;
   if (oldmask & FE_ENABLE_UNDERFLOW)   mask |= kUnderflow;
   if (oldmask & FE_ENABLE_INEXACT  )   mask |= kInexact;
#endif

   return mask;
}

////////////////////////////////////////////////////////////////////////////////
/// Set which conditions trigger a floating point exception.
/// Return the previous set of conditions.

Int_t TUnixSystem::SetFPEMask(Int_t mask)
{
   if (mask) { }  // use mask to avoid warning

   Int_t old = GetFPEMask();

#if defined(R__LINUX) && !defined(__powerpc__)
#if defined(__GLIBC__) && (__GLIBC__>2 || __GLIBC__==2 && __GLIBC_MINOR__>=1)
   Int_t newm = 0;
   if (mask & kInvalid  )   newm |= FE_INVALID;
   if (mask & kDivByZero)   newm |= FE_DIVBYZERO;
   if (mask & kOverflow )   newm |= FE_OVERFLOW;
   if (mask & kUnderflow)   newm |= FE_UNDERFLOW;
# ifdef FE_INEXACT
   if (mask & kInexact  )   newm |= FE_INEXACT;
# endif

#if __GLIBC_MINOR__>=3

   // clear pending exceptions so feenableexcept does not trigger them
   feclearexcept(FE_ALL_EXCEPT);
   fedisableexcept(FE_ALL_EXCEPT);
   feenableexcept(newm);

#else

   fenv_t cur;
   fegetenv(&cur);
#if defined __ia64__
   cur &= ~newm;
#else
   cur.__control_word &= ~newm;
#endif
   fesetenv(&cur);

#endif
#endif
#endif

#if defined(R__MACOSX) && defined(__SSE2__)
   // OS X uses the SSE unit for all FP math by default, not the x87 FP unit
   Int_t newm = 0;
   if (mask & kInvalid  )   newm |= _MM_MASK_INVALID;
   if (mask & kDivByZero)   newm |= _MM_MASK_DIV_ZERO;
   if (mask & kOverflow )   newm |= _MM_MASK_OVERFLOW;
   if (mask & kUnderflow)   newm |= _MM_MASK_UNDERFLOW;
   if (mask & kInexact  )   newm |= _MM_MASK_INEXACT;

   _MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() & ~newm);
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || defined(__arm64__))
   Int_t newm = 0;
   if (mask & kInvalid  )   newm |= FE_INVALID;
   if (mask & kDivByZero)   newm |= FE_DIVBYZERO;
   if (mask & kOverflow )   newm |= FE_OVERFLOW;
   if (mask & kUnderflow)   newm |= FE_UNDERFLOW;
   if (mask & kInexact  )   newm |= FE_INEXACT;

   fenv_t cur;
   fegetenv(&cur);
#if defined(__arm__)
   cur.__fpscr &= ~newm;
#elif defined(__arm64__)
   cur.__fpcr &= ~newm;
#else
   cur.__control &= ~newm;
#endif
   fesetenv(&cur);
#endif

#if defined(R__MACOSX) && !defined(__SSE2__) && !defined(__xlC__) && \
    !defined(__i386__) && !defined(__x86_64__) && !defined(__arm__) && \
    !defined(__arm64__)
   Int_t newm = 0;
   if (mask & kInvalid  )   newm |= FE_ENABLE_INVALID;
   if (mask & kDivByZero)   newm |= FE_ENABLE_DIVBYZERO;
   if (mask & kOverflow )   newm |= FE_ENABLE_OVERFLOW;
   if (mask & kUnderflow)   newm |= FE_ENABLE_UNDERFLOW;
   if (mask & kInexact  )   newm |= FE_ENABLE_INEXACT;

   Long64_t curmask;
   fegetenvd(curmask);
   curmask = (curmask & ~FE_ENABLE_ALL_EXCEPT) | newm;
   fesetenvd(curmask);
#endif

   return old;
}

//---- handling of system events -----------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Check if some signals were raised and call their Notify() member.

Bool_t TUnixSystem::CheckSignals(Bool_t sync)
{
   TSignalHandler *sh;
   Int_t sigdone = -1;
   {
      TOrdCollectionIter it((TOrdCollection*)fSignalHandler);

      while ((sh = (TSignalHandler*)it.Next())) {
         if (sync == sh->IsSync()) {
            ESignals sig = sh->GetSignal();
            if ((fSignals->IsSet(sig) && sigdone == -1) || sigdone == sig) {
               if (sigdone == -1) {
                  fSignals->Clr(sig);
                  sigdone = sig;
                  fSigcnt--;
               }
               if (sh->IsActive())
                  sh->Notify();
            }
         }
      }
   }
   if (sigdone != -1)
      return kTRUE;

   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Check if children have finished.

void TUnixSystem::CheckChilds()
{

}

////////////////////////////////////////////////////////////////////////////////
/// Check if there is activity on some file descriptors and call their
/// Notify() member.

Bool_t TUnixSystem::CheckDescriptors()
{
   return kFALSE;
}

//---- Directories -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Make a Unix file system directory. Returns 0 in case of success and
/// -1 if the directory could not be created.

int TUnixSystem::MakeDirectory(const char *name)
{
   TSystem *helper = FindHelper(name);
   if (helper)
      return helper->MakeDirectory(name);

   return UnixMakedir(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Open a Unix file system directory. Returns 0 if directory does not exist.

void *TUnixSystem::OpenDirectory(const char *name)
{
   TSystem *helper = FindHelper(name);
   if (helper)
      return helper->OpenDirectory(name);

   return UnixOpendir(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Close a Unix file system directory.

void TUnixSystem::FreeDirectory(void *dirp)
{
   TSystem *helper = FindHelper(0, dirp);
   if (helper) {
      helper->FreeDirectory(dirp);
      return;
   }

   if (dirp)
      ::closedir((DIR*)dirp);
}

////////////////////////////////////////////////////////////////////////////////
/// Get next Unix file system directory entry. Returns 0 if no more entries.

const char *TUnixSystem::GetDirEntry(void *dirp)
{
   TSystem *helper = FindHelper(0, dirp);
   if (helper)
      return helper->GetDirEntry(dirp);

   if (dirp)
      return UnixGetdirentry(dirp);

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Change directory. Returns kTRUE in case of success, kFALSE otherwise.

Bool_t TUnixSystem::ChangeDirectory(const char *path)
{
   Bool_t ret = (Bool_t) (::chdir(path) == 0);
   if (fWdpath != "")
      fWdpath = "";   // invalidate path cache
   return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// Return working directory.

const char *TUnixSystem::WorkingDirectory()
{
   // don't use cache as user can call chdir() directly somewhere else
   //if (fWdpath != "")
   //   return fWdpath.Data();

   R__LOCKGUARD2(gSystemMutex);

   static char cwd[kMAXPATHLEN];
   FillWithCwd(cwd);
   fWdpath = cwd;

   return fWdpath.Data();
}

//////////////////////////////////////////////////////////////////////////////
/// Return working directory.

std::string TUnixSystem::GetWorkingDirectory() const
{
   char cwd[kMAXPATHLEN];
   FillWithCwd(cwd);
   return std::string(cwd);
}

//////////////////////////////////////////////////////////////////////////////
/// Fill buffer with current working directory.

void TUnixSystem::FillWithCwd(char *cwd) const
{
   if (::getcwd(cwd, kMAXPATHLEN) == 0) {
      Error("WorkingDirectory", "getcwd() failed");
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return the user's home directory.

const char *TUnixSystem::HomeDirectory(const char *userName)
{
   return UnixHomedirectory(userName);
}

//////////////////////////////////////////////////////////////////////////////
/// Return the user's home directory.

std::string TUnixSystem::GetHomeDirectory(const char *userName) const
{
   char path[kMAXPATHLEN], mydir[kMAXPATHLEN] = { '\0' };
   auto res = UnixHomedirectory(userName, path, mydir);
   if (res) return std::string(res);
   else return std::string();
}

////////////////////////////////////////////////////////////////////////////////
/// Return a user configured or systemwide directory to create
/// temporary files in.

const char *TUnixSystem::TempDirectory() const
{
   const char *dir = gSystem->Getenv("TMPDIR");
   if (!dir || gSystem->AccessPathName(dir, kWritePermission))
      dir = "/tmp";

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

FILE *TUnixSystem::TempFileName(TString &base, const char *dir)
{
   char *b = ConcatFileName(dir ? dir : TempDirectory(), base);
   base = b;
   base += "XXXXXX";
   delete [] b;

   char *arg = StrDup(base);
   int fd = mkstemp(arg);
   base = arg;
   delete [] arg;

   if (fd == -1) {
      SysError("TempFileName", "%s", base.Data());
      return 0;
   } else {
      FILE *fp = fdopen(fd, "w+");
      if (fp == 0)
         SysError("TempFileName", "converting filedescriptor (%d)", fd);
      return fp;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Concatenate a directory and a file name.

const char *TUnixSystem::PrependPathName(const char *dir, TString& name)
{
   if (name.IsNull() || name == ".") {
      if (dir) {
         name = dir;
         if (dir[strlen(dir) - 1] != '/')
            name += '/';
      } else name = "";
      return name.Data();
   }

   if (!dir || !dir[0]) dir = "/";
   else if (dir[strlen(dir) - 1] != '/')
      name.Prepend('/');
   name.Prepend(dir);

   return name.Data();
}

//---- Paths & Files -----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Returns FALSE if one can access a file using the specified access mode.
/// Mode is the same as for the Unix access(2) function.
/// Attention, bizarre convention of return value!!

Bool_t TUnixSystem::AccessPathName(const char *path, EAccessMode mode)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->AccessPathName(path, mode);

   if (::access(StripOffProto(path, "file:"), mode) == 0)
      return kFALSE;
   GetLastErrorString() = GetError();

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Copy a file. If overwrite is true and file already exists the
/// file will be overwritten. Returns 0 when successful, -1 in case
/// of file open failure, -2 in case the file already exists and overwrite
/// was false and -3 in case of error during copy.

int TUnixSystem::CopyFile(const char *f, const char *t, Bool_t overwrite)
{
   if (!AccessPathName(t) && !overwrite)
      return -2;

   FILE *from = fopen(f, "r");
   if (!from)
      return -1;

   FILE *to   = fopen(t, "w");
   if (!to) {
      fclose(from);
      return -1;
   }

   const int bufsize = 1024;
   char buf[bufsize];
   int ret = 0;
   while (!ret && !feof(from)) {
      size_t numread    = fread (buf, sizeof(char), bufsize, from);
      size_t numwritten = fwrite(buf, sizeof(char), numread, to);
      if (numread != numwritten)
         ret = -3;
   }

   fclose(from);
   fclose(to);

   return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// Rename a file. Returns 0 when successful, -1 in case of failure.

int TUnixSystem::Rename(const char *f, const char *t)
{
   int ret = ::rename(f, t);
   GetLastErrorString() = GetError();
   return ret;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns TRUE if the url in 'path' points to the local file system.
/// This is used to avoid going through the NIC card for local operations.

Bool_t TUnixSystem::IsPathLocal(const char *path)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->IsPathLocal(path);

   return TSystem::IsPathLocal(path);
}

////////////////////////////////////////////////////////////////////////////////
/// Get info about a file. Info is returned in the form of a FileStat_t
/// structure (see TSystem.h).
/// The function returns 0 in case of success and 1 if the file could
/// not be stat'ed.

int TUnixSystem::GetPathInfo(const char *path, FileStat_t &buf)
{
   TSystem *helper = FindHelper(path);
   if (helper)
      return helper->GetPathInfo(path, buf);

   return UnixFilestat(path, buf);
}

////////////////////////////////////////////////////////////////////////////////
/// Get info about a file system: id, bsize, bfree, blocks.
/// Id      is file system type (machine dependend, see statfs())
/// Bsize   is block size of file system
/// Blocks  is total number of blocks in file system
/// Bfree   is number of free blocks in file system
/// The function returns 0 in case of success and 1 if the file system could
/// not be stat'ed.

int TUnixSystem::GetFsInfo(const char *path, Long_t *id, Long_t *bsize,
                           Long_t *blocks, Long_t *bfree)
{
   return UnixFSstat(path, id, bsize, blocks, bfree);
}

////////////////////////////////////////////////////////////////////////////////
/// Create a link from file1 to file2. Returns 0 when successful,
/// -1 in case of failure.

int TUnixSystem::Link(const char *from, const char *to)
{
   return ::link(from, to);
}

////////////////////////////////////////////////////////////////////////////////
/// Create a symlink from file1 to file2. Returns 0 when successful,
/// -1 in case of failure.

int TUnixSystem::Symlink(const char *from, const char *to)
{
#if defined(R__AIX)
   return ::symlink((char*)from, (char*)to);
#else
   return ::symlink(from, to);
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Unlink, i.e. remove, a file or directory. Returns 0 when successful,
/// -1 in case of failure.

int TUnixSystem::Unlink(const char *name)
{
   TSystem *helper = FindHelper(name);
   if (helper)
      return helper->Unlink(name);

#if defined(R__SEEK64)
   struct stat64 finfo;
   if (lstat64(name, &finfo) < 0)
#else
   struct stat finfo;
   if (lstat(name, &finfo) < 0)
#endif
      return -1;

   if (S_ISDIR(finfo.st_mode))
      return ::rmdir(name);
   else
      return ::unlink(name);
}

//---- expand the metacharacters as in the shell -------------------------------

// expand the metacharacters as in the shell

const char
#ifdef G__OLDEXPAND
   kShellEscape     = '\\',
   *kShellStuff     = "(){}<>\"'",
#endif
   *kShellMeta      = "~*[]{}?$";


#ifndef G__OLDEXPAND
////////////////////////////////////////////////////////////////////////////////
/// Expand a pathname getting rid of special shell characters like ~.$, etc.
/// For Unix/Win32 compatibility use $(XXX) instead of $XXX when using
/// environment variables in a pathname. If compatibility is not an issue
/// you can use on Unix directly $XXX. Returns kFALSE in case of success
/// or kTRUE in case of error.

Bool_t TUnixSystem::ExpandPathName(TString &path)
{
   const char *p, *patbuf = (const char *)path;

   // skip leading blanks
   while (*patbuf == ' ')
      patbuf++;

   // any shell meta characters ?
   for (p = patbuf; *p; p++)
      if (strchr(kShellMeta, *p))
         goto expand;

   return kFALSE;

expand:
   // replace $(XXX) by $XXX
   path.ReplaceAll("$(","$");
   path.ReplaceAll(")","");

   return ExpandFileName(path);
}
#endif

#ifdef G__OLDEXPAND
////////////////////////////////////////////////////////////////////////////////
/// Expand a pathname getting rid of special shell characters like ~.$, etc.
/// For Unix/Win32 compatibility use $(XXX) instead of $XXX when using
/// environment variables in a pathname. If compatibility is not an issue
/// you can use on Unix directly $XXX. Returns kFALSE in case of success
/// or kTRUE in case of error.

Bool_t TUnixSystem::ExpandPathName(TString &patbuf0)
{
   const char *patbuf = (const char *)patbuf0;
   const char *hd, *p;
   //   char   cmd[kMAXPATHLEN],
   char stuffedPat[kMAXPATHLEN], name[70];
   char  *q;
   FILE  *pf;
   int    ch;

   // skip leading blanks
   while (*patbuf == ' ')
      patbuf++;

   // any shell meta characters ?
   for (p = patbuf; *p; p++)
      if (strchr(kShellMeta, *p))
         goto needshell;

   return kFALSE;

needshell:
   // replace $(XXX) by $XXX
   patbuf0.ReplaceAll("$(","$");
   patbuf0.ReplaceAll(")","");

   // escape shell quote characters
   EscChar(patbuf, stuffedPat, sizeof(stuffedPat), (char*)kShellStuff, kShellEscape);

   TString cmd("echo ");

   // emulate csh -> popen executes sh
   if (stuffedPat[0] == '~') {
      if (stuffedPat[1] != '\0' && stuffedPat[1] != '/') {
         // extract user name
         for (p = &stuffedPat[1], q = name; *p && *p !='/';)
            *q++ = *p++;
         *q = '\0';
         hd = UnixHomedirectory(name);
         if (hd == 0)
            cmd += stuffedPat;
         else {
            cmd += hd;
            cmd += p;
         }
      } else {
         hd = UnixHomedirectory(0);
         if (hd == 0) {
            GetLastErrorString() = GetError();
            return kTRUE;
         }
         cmd += hd;
         cmd += &stuffedPat[1];
      }
   } else
      cmd += stuffedPat;

   if ((pf = ::popen(cmd.Data(), "r")) == 0) {
      GetLastErrorString() = GetError();
      return kTRUE;
   }

   // read first argument
   patbuf0 = "";
   int cnt = 0;
#if defined(R__AIX)
again:
#endif
   for (ch = fgetc(pf); ch != EOF && ch != ' ' && ch != '\n'; ch = fgetc(pf)) {
      patbuf0.Append(ch);
      cnt++;
   }
#if defined(R__AIX)
   // Work around bug timing problem due to delay in forking a large program
   if (cnt == 0 && ch == EOF) goto again;
#endif

   // skip rest of pipe
   while (ch != EOF) {
      ch = fgetc(pf);
      if (ch == ' ' || ch == '\t') {
         GetLastErrorString() = "expression ambigous";
         ::pclose(pf);
         return kTRUE;
      }
   }

   ::pclose(pf);

   return kFALSE;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// Expand a pathname getting rid of special shell characaters like ~.$, etc.
/// For Unix/Win32 compatibility use $(XXX) instead of $XXX when using
/// environment variables in a pathname. If compatibility is not an issue
/// you can use on Unix directly $XXX. The user must delete returned string.
/// Returns the expanded pathname or 0 in case of error.
/// The user must delete returned string (delete []).

char *TUnixSystem::ExpandPathName(const char *path)
{
   TString patbuf = path;
   if (ExpandPathName(patbuf))
      return 0;
   return StrDup(patbuf.Data());
}

////////////////////////////////////////////////////////////////////////////////
/// Set the file permission bits. Returns -1 in case or error, 0 otherwise.

int TUnixSystem::Chmod(const char *file, UInt_t mode)
{
   return ::chmod(file, mode);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the process file creation mode mask.

int TUnixSystem::Umask(Int_t mask)
{
   return ::umask(mask);
}

////////////////////////////////////////////////////////////////////////////////
/// Set a files modification and access times. If actime = 0 it will be
/// set to the modtime. Returns 0 on success and -1 in case of error.

int TUnixSystem::Utime(const char *file, Long_t modtime, Long_t actime)
{
   if (!actime)
      actime = modtime;

   struct utimbuf t;
   t.actime  = (time_t)actime;
   t.modtime = (time_t)modtime;
   return ::utime(file, &t);
}

////////////////////////////////////////////////////////////////////////////////
/// Find location of file "wfil" in a search path.
/// The search path is specified as a : separated list of directories.
/// Return value is pointing to wfile for compatibility with
/// Which(const char*,const char*,EAccessMode) version.

const char *TUnixSystem::FindFile(const char *search, TString& wfil, EAccessMode mode)
{
   TString show;
   if (gEnv->GetValue("Root.ShowPath", 0))
      show.Form("Which: %s =", wfil.Data());

   gSystem->ExpandPathName(wfil);

   if (wfil[0] == '/') {
#if defined(R__SEEK64)
      struct stat64 finfo;
      if (access(wfil.Data(), mode) == 0 &&
          stat64(wfil.Data(), &finfo) == 0 && S_ISREG(finfo.st_mode)) {
#else
      struct stat finfo;
      if (access(wfil.Data(), mode) == 0 &&
          stat(wfil.Data(), &finfo) == 0 && S_ISREG(finfo.st_mode)) {
#endif
         if (show != "")
            Printf("%s %s", show.Data(), wfil.Data());
         return wfil.Data();
      }
      if (show != "")
         Printf("%s <not found>", show.Data());
      wfil = "";
      return 0;
   }

   if (search == 0)
      search = ".";

   TString apwd(gSystem->WorkingDirectory());
   apwd += "/";
   for (const char* ptr = search; *ptr;) {
      TString name;
      if (*ptr != '/' && *ptr !='$' && *ptr != '~')
         name = apwd;
      const char* posEndOfPart = strchr(ptr, ':');
      if (posEndOfPart) {
         name.Append(ptr, posEndOfPart - ptr);
         ptr = posEndOfPart + 1; // skip ':'
      } else {
         name.Append(ptr);
         ptr += strlen(ptr);
      }

      if (!name.EndsWith("/"))
         name += '/';
      name += wfil;

      gSystem->ExpandPathName(name);
#if defined(R__SEEK64)
      struct stat64 finfo;
      if (access(name.Data(), mode) == 0 &&
          stat64(name.Data(), &finfo) == 0 && S_ISREG(finfo.st_mode)) {
#else
      struct stat finfo;
      if (access(name.Data(), mode) == 0 &&
          stat(name.Data(), &finfo) == 0 && S_ISREG(finfo.st_mode)) {
#endif
         if (show != "")
            Printf("%s %s", show.Data(), name.Data());
         wfil = name;
         return wfil.Data();
      }
   }

   if (show != "")
      Printf("%s <not found>", show.Data());
   wfil = "";
   return 0;
}

//---- Users & Groups ----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Returns the user's id. If user = 0, returns current user's id.

Int_t TUnixSystem::GetUid(const char *user)
{
   if (!user || !user[0])
      return getuid();
   else {
      struct passwd *apwd = getpwnam(user);
      if (apwd)
         return apwd->pw_uid;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the effective user id. The effective id corresponds to the
/// set id bit on the file being executed.

Int_t TUnixSystem::GetEffectiveUid()
{
   return geteuid();
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the group's id. If group = 0, returns current user's group.

Int_t TUnixSystem::GetGid(const char *group)
{
   if (!group || !group[0])
      return getgid();
   else {
      struct group *grp = getgrnam(group);
      if (grp)
         return grp->gr_gid;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the effective group id. The effective group id corresponds
/// to the set id bit on the file being executed.

Int_t TUnixSystem::GetEffectiveGid()
{
   return getegid();
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all user info in the UserGroup_t structure. The returned
/// structure must be deleted by the user. In case of error 0 is returned.

UserGroup_t *TUnixSystem::GetUserInfo(Int_t uid)
{
   typedef std::map<Int_t /*uid*/, UserGroup_t> UserInfoCache_t;
   static UserInfoCache_t gUserInfo;

   UserInfoCache_t::const_iterator iUserInfo = gUserInfo.find(uid);
   if (iUserInfo != gUserInfo.end())
      return new UserGroup_t(iUserInfo->second);

   struct passwd *apwd = getpwuid(uid);
   if (apwd) {
      UserGroup_t *ug = new UserGroup_t;
      ug->fUid      = apwd->pw_uid;
      ug->fGid      = apwd->pw_gid;
      ug->fUser     = apwd->pw_name;
      ug->fPasswd   = apwd->pw_passwd;
      ug->fRealName = apwd->pw_gecos;
      ug->fShell    = apwd->pw_shell;
      UserGroup_t *gr = GetGroupInfo(apwd->pw_gid);
      if (gr) ug->fGroup = gr->fGroup;
      delete gr;

      gUserInfo[uid] = *ug;
      return ug;
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all user info in the UserGroup_t structure. If user = 0, returns
/// current user's id info. The returned structure must be deleted by the
/// user. In case of error 0 is returned.

UserGroup_t *TUnixSystem::GetUserInfo(const char *user)
{
   return GetUserInfo(GetUid(user));
}

////////////////////////////////////////////////////////////////////////////////
/// Returns all group info in the UserGroup_t structure. The only active
/// fields in the UserGroup_t structure for this call are:
///    fGid and fGroup
/// The returned structure must be deleted by the user. In case of
/// error 0 is returned.

UserGroup_t *TUnixSystem::GetGroupInfo(Int_t gid)
{
   struct group *grp = getgrgid(gid);
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

UserGroup_t *TUnixSystem::GetGroupInfo(const char *group)
{
   return GetGroupInfo(GetGid(group));
}

//---- environment manipulation ------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Set environment variable.

void TUnixSystem::Setenv(const char *name, const char *value)
{
   ::setenv(name, value, 1);
}

////////////////////////////////////////////////////////////////////////////////
/// Get environment variable.

const char *TUnixSystem::Getenv(const char *name)
{
   return ::getenv(name);
}

////////////////////////////////////////////////////////////////////////////////
/// Unset environment variable.

void TUnixSystem::Unsetenv(const char *name)
{
   ::unsetenv(name);
}

//---- Processes ---------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Execute a command.

int TUnixSystem::Exec(const char *shellcmd)
{
   return ::system(shellcmd);
}

////////////////////////////////////////////////////////////////////////////////
/// Open a pipe.

FILE *TUnixSystem::OpenPipe(const char *command, const char *mode)
{
   return ::popen(command, mode);
}

////////////////////////////////////////////////////////////////////////////////
/// Close the pipe.

int TUnixSystem::ClosePipe(FILE *pipe)
{
   return ::pclose(pipe);
}

////////////////////////////////////////////////////////////////////////////////
/// Get process id.

int TUnixSystem::GetPid()
{
   return ::getpid();
}

////////////////////////////////////////////////////////////////////////////////
/// Exit the application.

void TUnixSystem::Exit(int code, Bool_t mode)
{
   // Insures that the files and sockets are closed before any library is unloaded
   // and before emptying CINT.
   TROOT::ShutDown();

   if (mode)
      ::exit(code);
   else
      ::_exit(code);
}

////////////////////////////////////////////////////////////////////////////////
/// Abort the application.

void TUnixSystem::Abort(int)
{
   ::abort();
}


#ifdef R__MACOSX
/// Use CoreSymbolication to retrieve the stacktrace.
#include <mach/mach.h>
extern "C" {
  // Adapted from https://github.com/mountainstorm/CoreSymbolication
  // Under the hood the framework basically just calls through to a set of C++ libraries
  typedef struct {
    void* csCppData;
    void* csCppObj;
  } CSTypeRef;
  typedef CSTypeRef CSSymbolicatorRef;
  typedef CSTypeRef CSSourceInfoRef;
  typedef CSTypeRef CSSymbolOwnerRef;
  typedef CSTypeRef CSSymbolRef;

  CSSymbolicatorRef CSSymbolicatorCreateWithPid(pid_t pid);
  CSSymbolRef CSSymbolicatorGetSymbolWithAddressAtTime(CSSymbolicatorRef cs, vm_address_t addr, uint64_t time);
  CSSourceInfoRef CSSymbolicatorGetSourceInfoWithAddressAtTime(CSSymbolicatorRef cs, vm_address_t addr, uint64_t time);
  const char* CSSymbolGetName(CSSymbolRef sym);
  CSSymbolOwnerRef CSSymbolGetSymbolOwner(CSSymbolRef sym);
  const char* CSSymbolOwnerGetPath(CSSymbolOwnerRef symbol);
  const char* CSSourceInfoGetPath(CSSourceInfoRef info);
  int CSSourceInfoGetLineNumber(CSSourceInfoRef info);
}

bool CSTypeRefIdValid(CSTypeRef ref) {
   return ref.csCppData || ref.csCppObj;
}

void macosx_backtrace() {
void* addrlist[kMAX_BACKTRACE_DEPTH];
  // retrieve current stack addresses
  int numstacks = backtrace( addrlist, sizeof( addrlist ) / sizeof( void* ));

  CSSymbolicatorRef symbolicator = CSSymbolicatorCreateWithPid(getpid());

  // skip TUnixSystem::Backtrace(), macosx_backtrace()
  static const int skipFrames = 2;
  for (int i = skipFrames; i < numstacks; ++i) {
    // No debug info, try to get at least the symbol name.
    CSSymbolRef sym = CSSymbolicatorGetSymbolWithAddressAtTime(symbolicator,
                                                               (vm_address_t)addrlist[i],
                                                               0x80000000u);
    CSSymbolOwnerRef symOwner = CSSymbolGetSymbolOwner(sym);

    if (const char* libPath = CSSymbolOwnerGetPath(symOwner)) {
      printf("[%s]", libPath);
    } else {
      printf("[<unknown binary>]");
    }

    if (const char* symname = CSSymbolGetName(sym)) {
      printf(" %s", symname);
    }

    CSSourceInfoRef sourceInfo
      = CSSymbolicatorGetSourceInfoWithAddressAtTime(symbolicator,
                                                     (vm_address_t)addrlist[i],
                                                     0x80000000u /*"now"*/);
    if (const char* sourcePath = CSSourceInfoGetPath(sourceInfo)) {
      printf(" %s:%d", sourcePath, (int)CSSourceInfoGetLineNumber(sourceInfo));
    } else {
      printf(" (no debug info)");
    }
    printf("\n");
  }
}
#endif // R__MACOSX

////////////////////////////////////////////////////////////////////////////////
/// Print a stack trace.

void TUnixSystem::StackTrace()
{
   if (!gEnv->GetValue("Root.Stacktrace", 1))
      return;

#ifndef R__MACOSX
   TString gdbscript = gEnv->GetValue("Root.StacktraceScript", "");
   gdbscript = gdbscript.Strip();
   if (gdbscript != "") {
      if (AccessPathName(gdbscript, kReadPermission)) {
         fprintf(stderr, "Root.StacktraceScript %s does not exist\n", gdbscript.Data());
         gdbscript = "";
      }
   }
   if (gdbscript == "") {
      gdbscript = "gdb-backtrace.sh";
      gSystem->PrependPathName(TROOT::GetEtcDir(), gdbscript);
      if (AccessPathName(gdbscript, kReadPermission)) {
         fprintf(stderr, "Error in <TUnixSystem::StackTrace> script %s is missing\n", gdbscript.Data());
         return;
      }
   }
   gdbscript += " ";

   TString gdbmess = gEnv->GetValue("Root.StacktraceMessage", "");
   gdbmess = gdbmess.Strip();

   std::cout.flush();
   fflush(stdout);

   std::cerr.flush();
   fflush(stderr);

   int fd = STDERR_FILENO;

   const char *message = " Generating stack trace...\n";

   if (fd && message) { }  // remove unused warning (remove later)

   if (gApplication && !strcmp(gApplication->GetName(), "TRint"))
      Getlinem(kCleanUp, 0);

#if defined(USE_GDB_STACK_TRACE)
   char *gdb = Which(Getenv("PATH"), "gdb", kExecutePermission);
   if (!gdb) {
      fprintf(stderr, "gdb not found, need it for stack trace\n");
      return;
   }

   // write custom message file
   TString gdbmessf = "gdb-message";
   if (gdbmess != "") {
      FILE *f = TempFileName(gdbmessf);
      fprintf(f, "%s\n", gdbmess.Data());
      fclose(f);
   }

   // use gdb to get stack trace
   gdbscript += GetExePath();
   gdbscript += " ";
   gdbscript += GetPid();
   if (gdbmess != "") {
      gdbscript += " ";
      gdbscript += gdbmessf;
   }
   gdbscript += " 1>&2";
   Exec(gdbscript);
   delete [] gdb;
   return;

#elif defined(R__AIX)
   TString script = "procstack ";
   script += GetPid();
   Exec(script);
   return;
#elif defined(R__SOLARIS)
   char *cppfilt = Which(Getenv("PATH"), "c++filt", kExecutePermission);
   TString script = "pstack ";
   script += GetPid();
   if (cppfilt) {
      script += " | ";
      script += cppfilt;
      delete [] cppfilt;
   }
   Exec(script);
   return;
#elif defined(HAVE_BACKTRACE_SYMBOLS_FD) && defined(HAVE_DLADDR)  // linux + MacOS X >= 10.5
   // we could have used backtrace_symbols_fd, except its output
   // format is pretty bad, so recode that here :-(

   // take care of demangling
   Bool_t demangle = kTRUE;

   // check for c++filt
   const char *cppfilt = "c++filt";
   const char *cppfiltarg = "";
#ifdef R__B64
   const char *format1 = " 0x%016lx in %.200s %s 0x%lx from %.200s\n";
#ifdef R__MACOSX
   const char *format2 = " 0x%016lx in %.200s\n";
#else
   const char *format2 = " 0x%016lx in %.200s at %.200s from %.200s\n";
#endif
   const char *format3 = " 0x%016lx in %.200s from %.200s\n";
   const char *format4 = " 0x%016lx in <unknown function>\n";
#else
   const char *format1 = " 0x%08lx in %.200s %s 0x%lx from %.200s\n";
#ifdef R__MACOSX
   const char *format2 = " 0x%08lx in %.200s\n";
#else
   const char *format2 = " 0x%08lx in %.200s at %.200s from %.200s\n";
#endif
   const char *format3 = " 0x%08lx in %.200s from %.200s\n";
   const char *format4 = " 0x%08lx in <unknown function>\n";
#endif

   char *filter = Which(Getenv("PATH"), cppfilt, kExecutePermission);
   if (!filter)
      demangle = kFALSE;

#if (__GNUC__ >= 3)
   // try finding supported format option for g++ v3
   if (filter) {
      FILE *p = OpenPipe(TString::Format("%s --help 2>&1", filter), "r");
      TString help;
      while (help.Gets(p)) {
         if (help.Index("gnu-v3") != kNPOS) {
            cppfiltarg = "--format=gnu-v3";
            break;
         } else if (help.Index("gnu-new-abi") != kNPOS) {
            cppfiltarg = "--format=gnu-new-abi";
            break;
         }
      }
      ClosePipe(p);
   }
#endif
   // gdb-backtrace.sh uses gdb to produce a backtrace. See if it is available.
   // If it is, use it. If not proceed as before.
#if (defined(R__LINUX) && !defined(R__WINGCC))
   // Declare the process that will be generating the stacktrace
   // For more see: http://askubuntu.com/questions/41629/after-upgrade-gdb-wont-attach-to-process
#ifdef PR_SET_PTRACER
   prctl(PR_SET_PTRACER, getpid(), 0, 0, 0);
#endif
#endif
   char *gdb = Which(Getenv("PATH"), "gdb", kExecutePermission);
   if (gdb) {
      // write custom message file
      TString gdbmessf = "gdb-message";
      if (gdbmess != "") {
         FILE *f = TempFileName(gdbmessf);
         fprintf(f, "%s\n", gdbmess.Data());
         fclose(f);
      }

      // use gdb to get stack trace
#ifdef R__MACOSX
      gdbscript += GetExePath();
      gdbscript += " ";
#endif
      gdbscript += GetPid();
      if (gdbmess != "") {
         gdbscript += " ";
         gdbscript += gdbmessf;
      }
      gdbscript += " 1>&2";
      Exec(gdbscript);
      delete [] gdb;
   } else {
      // addr2line uses debug info to convert addresses into file names
      // and line numbers
#ifdef R__MACOSX
      char *addr2line = Which(Getenv("PATH"), "atos", kExecutePermission);
#else
      char *addr2line = Which(Getenv("PATH"), "addr2line", kExecutePermission);
#endif
      if (addr2line) {
         // might take some time so tell what we are doing...
         if (write(fd, message, strlen(message)) < 0)
            Warning("StackTrace", "problems writing line numbers (errno: %d)", TSystem::GetErrno());
      }

      // open tmp file for demangled stack trace
      TString tmpf1 = "gdb-backtrace";
      std::ofstream file1;
      if (demangle) {
         FILE *f = TempFileName(tmpf1);
         if (f) fclose(f);
         file1.open(tmpf1);
         if (!file1) {
            Error("StackTrace", "could not open file %s", tmpf1.Data());
            Unlink(tmpf1);
            demangle = kFALSE;
         }
      }

#ifdef R__MACOSX
      if (addr2line)
         demangle = kFALSE;  // atos always demangles
#endif

      char buffer[4096];
      void *trace[kMAX_BACKTRACE_DEPTH];
      int  depth = backtrace(trace, kMAX_BACKTRACE_DEPTH);
      for (int n = 5; n < depth; n++) {
         ULong_t addr = (ULong_t) trace[n];
         Dl_info info;

         if (dladdr(trace[n], &info) && info.dli_fname && info.dli_fname[0]) {
            const char *libname = info.dli_fname;
            const char *symname = (info.dli_sname && info.dli_sname[0]) ?
                                   info.dli_sname : "<unknown>";
            ULong_t libaddr = (ULong_t) info.dli_fbase;
            ULong_t symaddr = (ULong_t) info.dli_saddr;
            Bool_t  gte = (addr >= symaddr);
            ULong_t diff = (gte) ? addr - symaddr : symaddr - addr;
            if (addr2line && symaddr) {
               Bool_t nodebug = kTRUE;
#ifdef R__MACOSX
               if (libaddr) { }  // use libaddr
#if defined(MAC_OS_X_VERSION_10_10)
               snprintf(buffer, sizeof(buffer), "%s -p %d 0x%016lx", addr2line, GetPid(), addr);
#elif defined(MAC_OS_X_VERSION_10_9)
               // suppress deprecation warning with opti
               snprintf(buffer, sizeof(buffer), "%s -d -p %d 0x%016lx", addr2line, GetPid(), addr);
#else
               snprintf(buffer, sizeof(buffer), "%s -p %d 0x%016lx", addr2line, GetPid(), addr);
#endif
#else
               ULong_t offset = (addr >= libaddr) ? addr - libaddr :
                                                    libaddr - addr;
               TString name   = TString(libname);
               Bool_t noPath  = kFALSE;
               Bool_t noShare = kTRUE;
               if (name[0] != '/') noPath = kTRUE;
               if (name.Contains(".so") || name.Contains(".sl")) noShare = kFALSE;
               if (noShare) offset = addr;
               if (noPath)  name = "`which " + name + "`";
               snprintf(buffer, sizeof(buffer), "%s -e %s 0x%016lx", addr2line, name.Data(), offset);
#endif
               if (FILE *pf = ::popen(buffer, "r")) {
                  char buf[2048];
                  if (fgets(buf, 2048, pf)) {
                     buf[strlen(buf)-1] = 0;  // remove trailing \n
                     if (strncmp(buf, "??", 2)) {
#ifdef R__MACOSX
                        snprintf(buffer, sizeof(buffer), format2, addr, buf);
#else
                        snprintf(buffer, sizeof(buffer), format2, addr, symname, buf, libname);
#endif
                        nodebug = kFALSE;
                     }
                  }
                  ::pclose(pf);
               }
               if (nodebug)
                  snprintf(buffer, sizeof(buffer), format1, addr, symname,
                           gte ? "+" : "-", diff, libname);
            } else {
               if (symaddr)
                  snprintf(buffer, sizeof(buffer), format1, addr, symname,
                           gte ? "+" : "-", diff, libname);
               else
                  snprintf(buffer, sizeof(buffer), format3, addr, symname, libname);
            }
         } else {
            snprintf(buffer, sizeof(buffer), format4, addr);
         }

         if (demangle)
            file1 << buffer;
         else
            if (write(fd, buffer, ::strlen(buffer)) < 0)
               Warning("StackTrace", "problems writing buffer (errno: %d)", TSystem::GetErrno());
      }

      if (demangle) {
         TString tmpf2 = "gdb-backtrace";
         FILE *f = TempFileName(tmpf2);
         if (f) fclose(f);
         file1.close();
         snprintf(buffer, sizeof(buffer), "%s %s < %s > %s", filter, cppfiltarg, tmpf1.Data(), tmpf2.Data());
         Exec(buffer);
         std::ifstream file2(tmpf2);
         TString line;
         while (file2) {
            line = "";
            line.ReadString(file2);
            if (write(fd, line.Data(), line.Length()) < 0)
               Warning("StackTrace", "problems writing line (errno: %d)", TSystem::GetErrno());
         }
         file2.close();
         Unlink(tmpf1);
         Unlink(tmpf2);
      }

      delete [] addr2line;
   }
   delete [] filter;
#elif defined(HAVE_EXCPT_H) && defined(HAVE_PDSC_H) && \
                               defined(HAVE_RLD_INTERFACE_H) // tru64
   // Tru64 stack walk.  Uses the exception handling library and the
   // run-time linker's core functions (loader(5)).  FIXME: Tru64
   // should have _RLD_DLADDR like IRIX below.  Verify and update.

   char         buffer [128];
   sigcontext   context;
   int          rc = 0;

   exc_capture_context (&context);
   while (!rc && context.sc_pc) {
      // FIXME: Elf32?
      pdsc_crd *func, *base, *crd
         = exc_remote_lookup_function_entry(0, 0, context.sc_pc, 0, &func, &base);
      Elf32_Addr addr = PDSC_CRD_BEGIN_ADDRESS(base, func);
      // const char *name = _rld_address_to_name(addr);
      const char *name = "<unknown function>";
      sprintf(buffer, " 0x%012lx %.200s + 0x%lx\n",
              context.sc_pc, name, context.sc_pc - addr);
      write(fd, buffer, ::strlen(buffer));
      rc = exc_virtual_unwind(0, &context);
   }
#endif
#else //R__MACOSX
  macosx_backtrace();
#endif //R__MACOSX
}

//---- System Logging ----------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Open connection to system log daemon. For the use of the options and
/// facility see the Unix openlog man page.

void TUnixSystem::Openlog(const char *name, Int_t options, ELogFacility facility)
{
   int fac = 0;

   switch (facility) {
      case kLogLocal0:
         fac = LOG_LOCAL0;
         break;
      case kLogLocal1:
         fac = LOG_LOCAL1;
         break;
      case kLogLocal2:
         fac = LOG_LOCAL2;
         break;
      case kLogLocal3:
         fac = LOG_LOCAL3;
         break;
      case kLogLocal4:
         fac = LOG_LOCAL4;
         break;
      case kLogLocal5:
         fac = LOG_LOCAL5;
         break;
      case kLogLocal6:
         fac = LOG_LOCAL6;
         break;
      case kLogLocal7:
         fac = LOG_LOCAL7;
         break;
   }

   ::openlog(name, options, fac);
}

////////////////////////////////////////////////////////////////////////////////
/// Send mess to syslog daemon. Level is the logging level and mess the
/// message that will be written on the log.

void TUnixSystem::Syslog(ELogLevel level, const char *mess)
{
   // ELogLevel matches exactly the Unix values.
   ::syslog(level, "%s", mess);
}

////////////////////////////////////////////////////////////////////////////////
/// Close connection to system log daemon.

void TUnixSystem::Closelog()
{
   ::closelog();
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

Int_t TUnixSystem::RedirectOutput(const char *file, const char *mode,
                                  RedirectHandle_t *h)
{
   // Instance to be used if the caller does not passes 'h'
   static RedirectHandle_t loch;

   Int_t rc = 0;

   // Which handle to use ?
   RedirectHandle_t *xh = (h) ? h : &loch;

   if (file) {
      // Save the paths
      Bool_t outdone = kFALSE;
      if (xh->fStdOutTty.IsNull()) {
         const char *tty = ttyname(STDOUT_FILENO);
         if (tty) {
            xh->fStdOutTty = tty;
         } else {
            if ((xh->fStdOutDup = dup(STDOUT_FILENO)) < 0) {
               SysError("RedirectOutput", "could not 'dup' stdout (errno: %d)", TSystem::GetErrno());
               return -1;
            }
            outdone = kTRUE;
         }
      }
      if (xh->fStdErrTty.IsNull()) {
         const char *tty = ttyname(STDERR_FILENO);
         if (tty) {
            xh->fStdErrTty = tty;
         } else {
            if ((xh->fStdErrDup = dup(STDERR_FILENO)) < 0) {
               SysError("RedirectOutput", "could not 'dup' stderr (errno: %d)", TSystem::GetErrno());
               if (outdone && dup2(xh->fStdOutDup, STDOUT_FILENO) < 0) {
                  Warning("RedirectOutput", "could not restore stdout (back to original redirected"
                          " file) (errno: %d)", TSystem::GetErrno());
               }
               return -1;
            }
         }
      }

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

      // Redirect stdout & stderr
      if (freopen(file, m, stdout) == 0) {
         SysError("RedirectOutput", "could not freopen stdout (errno: %d)", TSystem::GetErrno());
         return -1;
      }
      if (freopen(file, m, stderr) == 0) {
         SysError("RedirectOutput", "could not freopen stderr (errno: %d)", TSystem::GetErrno());
         if (freopen(xh->fStdOutTty.Data(), "a", stdout) == 0)
            SysError("RedirectOutput", "could not restore stdout (errno: %d)", TSystem::GetErrno());
         return -1;
      }
   } else {
      // Restore stdout & stderr
      fflush(stdout);
      if (!(xh->fStdOutTty.IsNull())) {
         if (freopen(xh->fStdOutTty.Data(), "a", stdout) == 0) {
            SysError("RedirectOutput", "could not restore stdout (errno: %d)", TSystem::GetErrno());
            rc = -1;
         }
         xh->fStdOutTty = "";
      } else {
         if (close(STDOUT_FILENO) != 0) {
            SysError("RedirectOutput",
                     "problems closing STDOUT_FILENO (%d) before 'dup2' (errno: %d)",
                     STDOUT_FILENO, TSystem::GetErrno());
            rc = -1;
         }
         if (dup2(xh->fStdOutDup, STDOUT_FILENO) < 0) {
            SysError("RedirectOutput", "could not restore stdout (back to original redirected"
                     " file) (errno: %d)", TSystem::GetErrno());
            rc = -1;
         }
         if (close(xh->fStdOutDup) != 0) {
            SysError("RedirectOutput",
                     "problems closing temporary 'out' descriptor %d (errno: %d)",
                     TSystem::GetErrno(), xh->fStdOutDup);
            rc = -1;
         }
      }
      fflush(stderr);
      if (!(xh->fStdErrTty.IsNull())) {
         if (freopen(xh->fStdErrTty.Data(), "a", stderr) == 0) {
            SysError("RedirectOutput", "could not restore stderr (errno: %d)", TSystem::GetErrno());
            rc = -1;
         }
         xh->fStdErrTty = "";
      } else {
         if (close(STDERR_FILENO) != 0) {
            SysError("RedirectOutput",
                     "problems closing STDERR_FILENO (%d) before 'dup2' (errno: %d)",
                     STDERR_FILENO, TSystem::GetErrno());
            rc = -1;
         }
         if (dup2(xh->fStdErrDup, STDERR_FILENO) < 0) {
            SysError("RedirectOutput", "could not restore stderr (back to original redirected"
                     " file) (errno: %d)", TSystem::GetErrno());
            rc = -1;
         }
         if (close(xh->fStdErrDup) != 0) {
            SysError("RedirectOutput",
                     "problems closing temporary 'err' descriptor %d (errno: %d)",
                     TSystem::GetErrno(), xh->fStdErrDup);
            rc = -1;
         }
      }
      // Reset the static instance, if using that
      if (xh == &loch)
         xh->Reset();
   }
   return rc;
}

//---- dynamic loading and linking ---------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///dynamic linking of module

Func_t TUnixSystem::DynFindSymbol(const char * /*module*/, const char *entry)
{
   return TSystem::DynFindSymbol("*", entry);
}

////////////////////////////////////////////////////////////////////////////////
/// Load a shared library. Returns 0 on successful loading, 1 in
/// case lib was already loaded and -1 in case lib does not exist
/// or in case of error.

int TUnixSystem::Load(const char *module, const char *entry, Bool_t system)
{
   return TSystem::Load(module, entry, system);
}

////////////////////////////////////////////////////////////////////////////////
/// Unload a shared library.

void TUnixSystem::Unload(const char *module)
{
   if (module) { TSystem::Unload(module); }
}

////////////////////////////////////////////////////////////////////////////////
/// List symbols in a shared library.

void TUnixSystem::ListSymbols(const char * /*module*/, const char * /*regexp*/)
{
   Error("ListSymbols", "not yet implemented");
}

////////////////////////////////////////////////////////////////////////////////
/// List all loaded shared libraries.

void TUnixSystem::ListLibraries(const char *regexp)
{
   TSystem::ListLibraries(regexp);
}

////////////////////////////////////////////////////////////////////////////////
/// Get list of shared libraries loaded at the start of the executable.
/// Returns 0 in case list cannot be obtained or in case of error.

const char *TUnixSystem::GetLinkedLibraries()
{
   static TString linkedLibs;
   static Bool_t once = kFALSE;

   R__LOCKGUARD2(gSystemMutex);

   if (!linkedLibs.IsNull())
      return linkedLibs;

   if (once)
      return 0;

#if !defined(R__MACOSX)
   const char *exe = GetExePath();
   if (!exe || !*exe)
      return 0;
#endif

#if defined(R__MACOSX)
   DylibAdded(0, 0);
   linkedLibs = gLinkedDylibs;
#elif defined(R__LINUX) || defined(R__SOLARIS) || defined(R__AIX)
#if defined(R__WINGCC )
   const char *cLDD="cygcheck";
   const char *cSOEXT=".dll";
   size_t lenexe = strlen(exe);
   if (strcmp(exe + lenexe - 4, ".exe")
       && strcmp(exe + lenexe - 4, ".dll")) {
      // it's not a dll and exe doesn't end on ".exe";
      // need to add it for cygcheck to find it:
      char* longerexe = new char[lenexe + 5];
      strlcpy(longerexe, exe,lenexe+5);
      strlcat(longerexe, ".exe",lenexe+5);
      delete [] exe;
      exe = longerexe;
   }
   TRegexp sovers = "\\.so\\.[0-9]+";
#else
   const char *cLDD="ldd";
#if defined(R__AIX)
   const char *cSOEXT=".a";
   TRegexp sovers = "\\.a\\.[0-9]+";
#else
   const char *cSOEXT=".so";
   TRegexp sovers = "\\.so\\.[0-9]+";
#endif
#endif
   FILE *p = OpenPipe(TString::Format("%s '%s'", cLDD, exe), "r");
   if (p) {
      TString ldd;
      while (ldd.Gets(p)) {
         TString delim(" \t");
         TObjArray *tok = ldd.Tokenize(delim);

         // expected format:
         //    libCoreLegacy.so => /home/rdm/root/lib/libCoreLegacy.so (0x40017000)
         TObjString *solibName = (TObjString*)tok->At(2);
         if (!solibName) {
            // case where there is only one name of the list:
            //    /usr/platform/SUNW,UltraAX-i2/lib/libc_psr.so.1
            solibName = (TObjString*)tok->At(0);
         }
         if (solibName) {
            TString solib = solibName->String();
            Ssiz_t idx = solib.Index(sovers);
            if (solib.EndsWith(cSOEXT) || idx != kNPOS) {
               if (idx != kNPOS)
                  solib.Remove(idx+3);
               if (!AccessPathName(solib, kReadPermission)) {
                  if (!linkedLibs.IsNull())
                     linkedLibs += " ";
                  linkedLibs += solib;
               }
            }
         }
         delete tok;
      }
      ClosePipe(p);
   }
#endif

   once = kTRUE;

   if (linkedLibs.IsNull())
      return 0;

   return linkedLibs;
}

//---- Time & Date -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get current time in milliseconds since 0:00 Jan 1 1995.

TTime TUnixSystem::Now()
{
   return UnixNow();
}

//---- RPC ---------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get Internet Protocol (IP) address of host. Returns an TInetAddress
/// object. To see if the hostname lookup was successfull call
/// TInetAddress::IsValid().

TInetAddress TUnixSystem::GetHostByName(const char *hostname)
{
   TInetAddress ia;
   struct addrinfo hints;
   struct addrinfo *result, *rp;
   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_family = AF_INET;       // only IPv4
   hints.ai_socktype = 0;           // any socket type
   hints.ai_protocol = 0;           // any protocol
   hints.ai_flags = AI_CANONNAME;   // get canonical name
#ifdef R__MACOSX
   // Anything ending on ".local" causes a 5 second delay in getaddrinfo().
   // See e.g. https://apple.stackexchange.com/questions/175320/why-is-my-hostname-resolution-taking-so-long
   // Only reasonable solution: remove the "domain" part if it's ".local".
   size_t lenHostname = strlen(hostname);
   std::string hostnameWithoutLocal{hostname};
   if (lenHostname > 6 && !strcmp(hostname + lenHostname - 6, ".local")) {
      hostnameWithoutLocal.erase(lenHostname - 6);
      hostname = hostnameWithoutLocal.c_str();
   }
#endif

   // obsolete gethostbyname() replaced by getaddrinfo()
   int rc = getaddrinfo(hostname, nullptr, &hints, &result);
   if (rc != 0) {
      if (rc == EAI_NONAME) {
         if (gDebug > 0) Error("GetHostByName", "unknown host '%s'", hostname);
         ia.fHostname = "UnNamedHost";
      } else {
         Error("GetHostByName", "getaddrinfo failed for '%s': %s", hostname, gai_strerror(rc));
         ia.fHostname = "UnknownHost";
      }
      return ia;
   }

   std::string hostcanon(result->ai_canonname ? result->ai_canonname : hostname);
   ia.fHostname = hostcanon.data();
   ia.fFamily = result->ai_family;
   ia.fAddresses[0] = ntohl(((struct sockaddr_in *)(result->ai_addr))->sin_addr.s_addr);
   // with getaddrinfo() no way to get list of aliases for a hostname
   if (hostcanon.compare(hostname) != 0) ia.AddAlias(hostname);

   // check on numeric hostname
   char tmp[sizeof(struct in_addr)];
   if (inet_pton(AF_INET, hostcanon.data(), tmp) == 1) {
      char hbuf[NI_MAXHOST];
      if (getnameinfo(result->ai_addr, result->ai_addrlen, hbuf, sizeof(hbuf), nullptr, 0, 0) == 0)
         ia.fHostname = hbuf;
   }

   // check other addresses (if exist)
   rp = result->ai_next;
   for (; rp != nullptr; rp = rp->ai_next) {
      UInt_t arp = ntohl(((struct sockaddr_in *)(rp->ai_addr))->sin_addr.s_addr);
      if ( !(std::find(ia.fAddresses.begin(), ia.fAddresses.end(), arp) != ia.fAddresses.end()) )
         ia.AddAddress(arp);
   }

   freeaddrinfo(result);
   return ia;
}

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Static Protected Unix Interface functions.                           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

//---- signals -----------------------------------------------------------------

static struct Signalmap_t {
   int               fCode;
   SigHandler_t      fHandler;
   struct sigaction *fOldHandler;
   const char       *fSigName;
} gSignalMap[kMAXSIGNALS] = {
   { SIGBUS,   0, 0, "bus error" },
   { SIGSEGV,  0, 0, "segmentation violation" },
   { SIGSYS,   0, 0, "bad argument to system call" },
   { SIGPIPE,  0, 0, "write on a pipe with no one to read it" },
   { SIGILL,   0, 0, "illegal instruction" },
   { SIGABRT,  0, 0, "abort" },
   { SIGQUIT,  0, 0, "quit" },
   { SIGINT,   0, 0, "interrupt" },
   { SIGWINCH, 0, 0, "window size change" },
   { SIGALRM,  0, 0, "alarm clock" },
   { SIGCHLD,  0, 0, "death of a child" },
   { SIGURG,   0, 0, "urgent data arrived on an I/O channel" },
   { SIGFPE,   0, 0, "floating point exception" },
   { SIGTERM,  0, 0, "termination signal" },
   { SIGUSR1,  0, 0, "user-defined signal 1" },
   { SIGUSR2,  0, 0, "user-defined signal 2" }
};


////////////////////////////////////////////////////////////////////////////////
/// Call the signal handler associated with the signal.

static void sighandler(int sig)
{
   for (int i= 0; i < kMAXSIGNALS; i++) {
      if (gSignalMap[i].fCode == sig) {
         (*gSignalMap[i].fHandler)((ESignals)i);
         return;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Handle and dispatch signals.

void TUnixSystem::DispatchSignals(ESignals sig)
{
   switch (sig) {
   case kSigChild:
      CheckChilds();
      break;
   case kSigBus:
   case kSigSegmentationViolation:
   case kSigIllegalInstruction:
   case kSigAbort:
   case kSigFloatingException:
      if (gExceptionHandler)
         //sig is ESignal, should it be mapped to the correct signal number?
         gExceptionHandler->HandleException(sig);
      else {
         //map to the real signal code + set the
         //high order bit to indicate a signal (?)
         Break("TUnixSystem::DispatchSignals", "%s", UnixSigname(sig));
         StackTrace();
         Exit(gSignalMap[sig].fCode + 0x80);
      }
      break;
   case kSigSystem:
   case kSigPipe:
      Break("TUnixSystem::DispatchSignals", "%s", UnixSigname(sig));
      break;
   case kSigWindowChanged:
      Gl_windowchanged();
      break;
   case kSigUser2:
      Break("TUnixSystem::DispatchSignals", "%s: printing stacktrace", UnixSigname(sig));
      StackTrace();
      // Intentional fall-through; pass the signal to handlers (or terminate):
   default:
      fSignals->Set(sig);
      fSigcnt++;
      break;
   }

   // check a-synchronous signals
   if (fSigcnt > 0 && fSignalHandler->GetSize() > 0)
      CheckSignals(kFALSE);
}

////////////////////////////////////////////////////////////////////////////////
/// Set a signal handler for a signal.

void TUnixSystem::UnixSignal(ESignals sig, SigHandler_t handler)
{
   if (gEnv && !gEnv->GetValue("Root.ErrorHandlers", 1))
      return;

   if (gSignalMap[sig].fHandler != handler) {
      struct sigaction sigact;

      gSignalMap[sig].fHandler    = handler;
      gSignalMap[sig].fOldHandler = new struct sigaction();

#if defined(R__SUN)
      sigact.sa_handler = (void (*)())sighandler;
#elif defined(R__SOLARIS)
      sigact.sa_handler = sighandler;
#elif defined(R__LYNXOS)
#  if (__GNUG__>=3)
      sigact.sa_handler = sighandler;
#  else
      sigact.sa_handler = (void (*)(...))sighandler;
#  endif
#else
      sigact.sa_handler = sighandler;
#endif
      sigemptyset(&sigact.sa_mask);
      sigact.sa_flags = 0;
#if defined(SA_RESTART)
      sigact.sa_flags |= SA_RESTART;
#endif
      if (sigaction(gSignalMap[sig].fCode, &sigact,
                    gSignalMap[sig].fOldHandler) < 0)
         ::CppyyLegacy::SysError("TUnixSystem::UnixSignal", "sigaction");
   }
}

////////////////////////////////////////////////////////////////////////////////
/// If ignore is true ignore the specified signal, else restore previous
/// behaviour.

void TUnixSystem::UnixIgnoreSignal(ESignals sig, Bool_t ignore)
{
   TTHREAD_TLS(Bool_t) ignoreSig[kMAXSIGNALS] = { kFALSE };
   TTHREAD_TLS_ARRAY(struct sigaction,kMAXSIGNALS,oldsigact);

   if (ignore != ignoreSig[sig]) {
      ignoreSig[sig] = ignore;
      if (ignore) {
         struct sigaction sigact;
#if defined(R__SUN)
         sigact.sa_handler = (void (*)())SIG_IGN;
#elif defined(R__SOLARIS)
         sigact.sa_handler = (void (*)(int))SIG_IGN;
#else
         sigact.sa_handler = SIG_IGN;
#endif
         sigemptyset(&sigact.sa_mask);
         sigact.sa_flags = 0;
         if (sigaction(gSignalMap[sig].fCode, &sigact, &oldsigact[sig]) < 0)
            ::CppyyLegacy::SysError("TUnixSystem::UnixIgnoreSignal", "sigaction");
      } else {
         if (sigaction(gSignalMap[sig].fCode, &oldsigact[sig], 0) < 0)
            ::CppyyLegacy::SysError("TUnixSystem::UnixIgnoreSignal", "sigaction");
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/// When the argument is true the SIGALRM signal handler is set so that
/// interrupted syscalls will not be restarted by the kernel. This is
/// typically used in case one wants to put a timeout on an I/O operation.
/// By default interrupted syscalls will always be restarted (for all

void TUnixSystem::UnixSigAlarmInterruptsSyscalls(Bool_t set)
{
   if (gSignalMap[kSigAlarm].fHandler) {
      struct sigaction sigact;
#if defined(R__SUN)
      sigact.sa_handler = (void (*)())sighandler;
#elif defined(R__SOLARIS)
      sigact.sa_handler = sighandler;
#elif defined(R__LYNXOS)
#  if (__GNUG__>=3)
      sigact.sa_handler = sighandler;
#  else
      sigact.sa_handler = (void (*)(...))sighandler;
#  endif
#else
      sigact.sa_handler = sighandler;
#endif
      sigemptyset(&sigact.sa_mask);
      sigact.sa_flags = 0;
      if (set) {
#if defined(SA_INTERRUPT)       // SunOS
         sigact.sa_flags |= SA_INTERRUPT;
#endif
      } else {
#if defined(SA_RESTART)
         sigact.sa_flags |= SA_RESTART;
#endif
      }
      if (sigaction(gSignalMap[kSigAlarm].fCode, &sigact, 0) < 0)
         ::CppyyLegacy::SysError("TUnixSystem::UnixSigAlarmInterruptsSyscalls", "sigaction");
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return the signal name associated with a signal.

const char *TUnixSystem::UnixSigname(ESignals sig)
{
   return gSignalMap[sig].fSigName;
}

////////////////////////////////////////////////////////////////////////////////
/// Restore old signal handler for specified signal.

void TUnixSystem::UnixResetSignal(ESignals sig)
{
   if (gSignalMap[sig].fOldHandler) {
      // restore old signal handler
      if (sigaction(gSignalMap[sig].fCode, gSignalMap[sig].fOldHandler, 0) < 0)
         ::CppyyLegacy::SysError("TUnixSystem::UnixSignal", "sigaction");
      delete gSignalMap[sig].fOldHandler;
      gSignalMap[sig].fOldHandler = 0;
      gSignalMap[sig].fHandler    = 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Restore old signal handlers.

void TUnixSystem::UnixResetSignals()
{
   for (int sig = 0; sig < kMAXSIGNALS; sig++)
      UnixResetSignal((ESignals)sig);
}

//---- time --------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get current time in milliseconds since 0:00 Jan 1 1995.

Long64_t TUnixSystem::UnixNow()
{
   static std::atomic<time_t> jan95{0};
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
         ::CppyyLegacy::SysError("TUnixSystem::UnixNow", "error converting 950001 0:00 to time_t");
         return 0;
      }
   }

   struct timeval t;
   gettimeofday(&t, 0);
   return Long64_t(t.tv_sec-(Long_t)jan95)*1000 + t.tv_usec/1000;
}

////////////////////////////////////////////////////////////////////////////////
/// Set interval timer to time-out in ms milliseconds.

int TUnixSystem::UnixSetitimer(Long_t ms)
{
   struct itimerval itv;
   itv.it_value.tv_sec     = 0;
   itv.it_value.tv_usec    = 0;
   itv.it_interval.tv_sec  = 0;
   itv.it_interval.tv_usec = 0;
   if (ms > 0) {
      itv.it_value.tv_sec  = time_t(ms / 1000);
      itv.it_value.tv_usec = time_t((ms % 1000) * 1000);
   }
   int st = setitimer(ITIMER_REAL, &itv, 0);
   if (st == -1)
      ::CppyyLegacy::SysError("TUnixSystem::UnixSetitimer", "setitimer");
   return st;
}

//---- file descriptors --------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Wait for events on the file descriptors specified in the readready and
/// writeready masks or for timeout (in milliseconds) to occur. Returns
/// the number of ready descriptors, or 0 in case of timeout, or < 0 in
/// case of an error, with -2 being EINTR and -3 EBADF. In case of EINTR
/// the errno has been reset and the method can be called again.

int TUnixSystem::UnixSelect(Int_t nfds, TFdSet *readready, TFdSet *writeready,
                            Long_t timeout)
{
   int retcode;

   fd_set *rd = (readready)  ? (fd_set*)readready->GetBits()  : 0;
   fd_set *wr = (writeready) ? (fd_set*)writeready->GetBits() : 0;

   if (timeout >= 0) {
      struct timeval tv;
      tv.tv_sec  = Int_t(timeout / 1000);
      tv.tv_usec = (timeout % 1000) * 1000;
      retcode = select(nfds, rd, wr, 0, &tv);
   } else {
      retcode = select(nfds, rd, wr, 0, 0);
   }
   if (retcode == -1) {
      if (GetErrno() == EINTR) {
         ResetErrno();  // errno is not self reseting
         return -2;
      }
      if (GetErrno() == EBADF)
         return -3;
      return -1;
   }

   return retcode;
}

//---- directories -------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Returns the user's home directory.

const char *TUnixSystem::UnixHomedirectory(const char *name)
{
   static char path[kMAXPATHLEN], mydir[kMAXPATHLEN] = { '\0' };
   return UnixHomedirectory(name, path, mydir);
}

////////////////////////////////////////////////////////////////////////////
/// Returns the user's home directory.

const char *TUnixSystem::UnixHomedirectory(const char *name, char *path, char *mydir)
{
   struct passwd *pw;
   if (name) {
      pw = getpwnam(name);
      if (pw) {
         strncpy(path, pw->pw_dir, kMAXPATHLEN-1);
         path[kMAXPATHLEN-1] = '\0';
         return path;
      }
   } else {
      if (mydir[0])
         return mydir;
      pw = getpwuid(getuid());
      if (pw && pw->pw_dir) {
         strncpy(mydir, pw->pw_dir, kMAXPATHLEN-1);
         mydir[kMAXPATHLEN-1] = '\0';
         return mydir;
      } else if (gSystem->Getenv("HOME")) {
         strncpy(mydir, gSystem->Getenv("HOME"), kMAXPATHLEN-1);
         mydir[kMAXPATHLEN-1] = '\0';
         return mydir;
      }
   }
   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Make a Unix file system directory. Returns 0 in case of success and
/// -1 if the directory could not be created (either already exists or
/// illegal path name).

int TUnixSystem::UnixMakedir(const char *dir)
{
   return ::mkdir(StripOffProto(dir, "file:"), 0755);
}

////////////////////////////////////////////////////////////////////////////////
/// Open a directory.

void *TUnixSystem::UnixOpendir(const char *dir)
{
   struct stat finfo;

   const char *edir = StripOffProto(dir, "file:");

   if (stat(edir, &finfo) < 0)
      return 0;

   if (!S_ISDIR(finfo.st_mode))
      return 0;

   return (void*) opendir(edir);
}

#if defined(_POSIX_SOURCE)
// Posix does not require that the d_ino field be present, and some
// systems do not provide it.
#   define REAL_DIR_ENTRY(dp) 1
#else
#   define REAL_DIR_ENTRY(dp) (dp->d_ino != 0)
#endif

////////////////////////////////////////////////////////////////////////////////
/// Returns the next directory entry.

const char *TUnixSystem::UnixGetdirentry(void *dirp1)
{
   DIR *dirp = (DIR*)dirp1;
#ifdef HAS_DIRENT
   struct dirent *dp;
#else
   struct direct *dp;
#endif

   if (dirp) {
      for (;;) {
         dp = readdir(dirp);
         if (dp == 0)
            return 0;
         if (REAL_DIR_ENTRY(dp))
            return dp->d_name;
      }
   }
   return 0;
}

//---- files -------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get info about a file. Info is returned in the form of a FileStat_t
/// structure (see TSystem.h).
/// The function returns 0 in case of success and 1 if the file could
/// not be stat'ed.

int TUnixSystem::UnixFilestat(const char *fpath, FileStat_t &buf)
{
   const char *path = StripOffProto(fpath, "file:");
   buf.fIsLink = kFALSE;

#if defined(R__SEEK64)
   struct stat64 sbuf;
   if (path && lstat64(path, &sbuf) == 0) {
#else
   struct stat sbuf;
   if (path && lstat(path, &sbuf) == 0) {
#endif
      buf.fIsLink = S_ISLNK(sbuf.st_mode);
      if (buf.fIsLink) {
#if defined(R__SEEK64)
         if (stat64(path, &sbuf) == -1) {
#else
         if (stat(path, &sbuf) == -1) {
#endif
            return 1;
         }
      }
      buf.fDev   = sbuf.st_dev;
      buf.fIno   = sbuf.st_ino;
      buf.fMode  = sbuf.st_mode;
      buf.fUid   = sbuf.st_uid;
      buf.fGid   = sbuf.st_gid;
      buf.fSize  = sbuf.st_size;
      buf.fMtime = sbuf.st_mtime;

      return 0;
   }
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

int TUnixSystem::UnixFSstat(const char *path, Long_t *id, Long_t *bsize,
                            Long_t *blocks, Long_t *bfree)
{
   struct statfs statfsbuf;
#if (defined(R__SOLARIS) && !defined(R__LINUX))
   if (statfs(path, &statfsbuf, sizeof(struct statfs), 0) == 0) {
      *id = statfsbuf.f_fstyp;
      *bsize = statfsbuf.f_bsize;
      *blocks = statfsbuf.f_blocks;
      *bfree = statfsbuf.f_bfree;
#else
   if (statfs((char*)path, &statfsbuf) == 0) {
#ifdef R__OBSD
      // Convert BSD filesystem names to Linux filesystem type numbers
      // where possible.  Linux statfs uses a value of -1 to indicate
      // an unsupported field.

      if (!strcmp(statfsbuf.f_fstypename, MOUNT_FFS) ||
          !strcmp(statfsbuf.f_fstypename, MOUNT_MFS))
         *id = 0x11954;
      else if (!strcmp(statfsbuf.f_fstypename, MOUNT_NFS))
         *id = 0x6969;
      else if (!strcmp(statfsbuf.f_fstypename, MOUNT_MSDOS))
         *id = 0x4d44;
      else if (!strcmp(statfsbuf.f_fstypename, MOUNT_EXT2FS))
         *id = 0xef53;
      else if (!strcmp(statfsbuf.f_fstypename, MOUNT_CD9660))
         *id = 0x9660;
      else if (!strcmp(statfsbuf.f_fstypename, MOUNT_NCPFS))
         *id = 0x6969;
      else
         *id = -1;
#else
      *id = statfsbuf.f_type;
#endif
      *bsize = statfsbuf.f_bsize;
      *blocks = statfsbuf.f_blocks;
      *bfree = statfsbuf.f_bavail;
#endif
      return 0;
   }
   return 1;
}

////////////////////////////////////////////////////////////////////////////////
/// Wait till child is finished.

int TUnixSystem::UnixWaitchild()
{
   int status;
   return (int) waitpid(0, &status, WNOHANG);
}

//---- Dynamic Loading ---------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// Get shared library search path. Static utility function.

static const char *DynamicPath(const char *newpath = 0, Bool_t reset = kFALSE)
{
   static TString dynpath;
   static Bool_t initialized = kFALSE;
   if (!initialized) {
      // force one time initialization of gROOT before we start
      // (otherwise it might be done as a side effect of gEnv->GetValue and
      // TROOT's initialization will call this routine).
      gROOT;
   }

   if (newpath) {
      dynpath = newpath;
   } else if (reset || !initialized) {
      initialized = kTRUE;
      TString rdynpath = gEnv->GetValue("Root.DynamicPath", (char*)0);
      rdynpath.ReplaceAll(": ", ":");  // in case DynamicPath was extended
      if (rdynpath.IsNull()) {
         rdynpath = ".:"; rdynpath += TROOT::GetLibDir();
      }
      TString ldpath;
#if defined (R__AIX)
      ldpath = gSystem->Getenv("LIBPATH");
#elif defined(R__MACOSX)
      ldpath = gSystem->Getenv("DYLD_LIBRARY_PATH");
      if (!ldpath.IsNull())
         ldpath += ":";
      ldpath += gSystem->Getenv("LD_LIBRARY_PATH");
      if (!ldpath.IsNull())
         ldpath += ":";
      ldpath += gSystem->Getenv("DYLD_FALLBACK_LIBRARY_PATH");
#else
      ldpath = gSystem->Getenv("LD_LIBRARY_PATH");
#endif
      if (ldpath.IsNull())
         dynpath = rdynpath;
      else {
         dynpath = ldpath; dynpath += ":"; dynpath += rdynpath;
      }
      if (!dynpath.Contains(TROOT::GetLibDir())) {
         dynpath += ":"; dynpath += TROOT::GetLibDir();
      }
      if (gCling) {
         dynpath += ":"; dynpath += gCling->GetSTLIncludePath();
      } else
         initialized = kFALSE;

#if defined(R__WINGCC) || defined(R__MACOSX)
      if (!dynpath.EndsWith(":")) dynpath += ":";
      dynpath += "/usr/local/lib:/usr/lib:/lib:";
      dynpath += "/lib/x86_64-linux-gnu:/usr/local/lib64:/usr/lib64:/lib64:";
#else
      // trick to get the system search path
      std::string cmd("LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-DOESNOTEXIST} LD_DEBUG=libs LD_PRELOAD=DOESNOTEXIST ls 2>&1");
      FILE *pf = popen(cmd.c_str (), "r");
      std::string result = "";
      char buffer[128];
      while (!feof(pf)) {
         if (fgets(buffer, 128, pf) != NULL)
            result += buffer;
      }
      pclose(pf);
      std::size_t from = result.find("search path=", result.find("(LD_LIBRARY_PATH)"));
      std::size_t to = result.find("(system search path)");
      if (from != std::string::npos && to != std::string::npos) {
         from += 12;
         std::string sys_path = result.substr(from, to-from);
         sys_path.erase(std::remove_if(sys_path.begin(), sys_path.end(), isspace), sys_path.end());
         if (!dynpath.EndsWith(":")) dynpath += ":";
         dynpath += sys_path.c_str();
      }
      dynpath.ReplaceAll("::", ":");
#endif
      if (gDebug > 0) std::cout << "dynpath = " << dynpath.Data() << std::endl;
   }
   return dynpath;
}

////////////////////////////////////////////////////////////////////////////////
/// Add a new directory to the dynamic path.

void TUnixSystem::AddDynamicPath(const char *path)
{
   if (path) {
      TString oldpath = DynamicPath(0, kFALSE);
      oldpath.Append(":");
      oldpath.Append(path);
      DynamicPath(oldpath);
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Return the dynamic path (used to find shared libraries).

const char *TUnixSystem::GetDynamicPath()
{
   return DynamicPath(0, kFALSE);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the dynamic path to a new value.
/// If the value of 'path' is zero, the dynamic path is reset to its
/// default value.

void TUnixSystem::SetDynamicPath(const char *path)
{
   if (!path)
      DynamicPath(0, kTRUE);
   else
      DynamicPath(path);
}

////////////////////////////////////////////////////////////////////////////////
/// Returns the path of a shared library (searches for library in the
/// shared library search path). If no file name extension is provided
/// it first tries .so, .sl, .dl and then .a (for AIX).

const char *TUnixSystem::FindDynamicLibrary(TString& sLib, Bool_t quiet)
{
   char buf[PATH_MAX + 1];
   char *res = realpath(sLib.Data(), buf);
   if (res) sLib = buf;
   TString searchFor = sLib;
   if (gSystem->FindFile(GetDynamicPath(), sLib, kReadPermission)) {
      return sLib;
   }
   sLib = searchFor;
   const char* lib = sLib.Data();
   int len = sLib.Length();
   if (len > 3 && (!strcmp(lib+len-3, ".so")    ||
                   !strcmp(lib+len-3, ".dl")    ||
                   !strcmp(lib+len-4, ".dll")   ||
                   !strcmp(lib+len-4, ".DLL")   ||
                   !strcmp(lib+len-6, ".dylib") ||
                   !strcmp(lib+len-3, ".sl")    ||
                   !strcmp(lib+len-2, ".a"))) {
      if (gSystem->FindFile(GetDynamicPath(), sLib, kReadPermission)) {
         return sLib;
      }
      if (!quiet)
         Error("FindDynamicLibrary",
               "%s does not exist in %s", searchFor.Data(), GetDynamicPath());
      return 0;
   }
   static const char* exts[] = {
      ".so", ".dll", ".dylib", ".sl", ".dl", ".a", 0 };
   const char** ext = exts;
   while (*ext) {
      TString fname(sLib);
      fname += *ext;
      ++ext;
      if (gSystem->FindFile(GetDynamicPath(), fname, kReadPermission)) {
         sLib.Swap(fname);
         return sLib;
      }
   }

   if (!quiet)
      Error("FindDynamicLibrary",
            "%s[.so | .dll | .dylib | .sl | .dl | .a] does not exist in %s",
            searchFor.Data(), GetDynamicPath());

   return 0;
}

} // namespace CppyyLegacy
