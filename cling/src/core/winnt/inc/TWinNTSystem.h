// @(#)root/winnt:$Id$
// Author: Fons Rademakers   15/09/95

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/


#ifndef ROOT_TWinNTSystem
#define ROOT_TWinNTSystem

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TWinNTSystem                                                         //
//                                                                      //
// Class providing an interface to the Windows NT Operating System.     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TSystem.h"

#if !defined(__CINT__)
 #include "Windows4Root.h"
 #include <commctrl.h>
#else
 typedef void* HANDLE;
 struct WIN32_FIND_DATA;
 typedef void* HIMAGELIST;
 typedef void* HICON;
 typedef UChar_t BOOL;
 struct FILE;
#endif

#ifndef MAX_SID_SIZE
#define MAX_SID_SIZE   1024
#endif
#define MAX_NAME_STRING   1024

#define SID_GROUP         0
#define SID_MEMBER        1

struct passwd {
   char *pw_name;       // user name
   char *pw_passwd;     // user password
   int   pw_uid;        // user ID
   int   pw_gid;        // user's group ID
   int   pw_quota;      //
   char *pw_gecos;      // user's real (long) name
   char *pw_dir;        // user home directory
   char *pw_shell;      // shell command
   char *pw_group;      // user's group name
};

struct group {
   char   *gr_name;     // group name
   char   *gr_passwd;   // group password
   int    gr_gid;       // group id
   char   **gr_mem;     // group members
};


namespace CppyyLegacy {

class TWinNTSystem : public TSystem {
public:
   // pointer to message handler func
   typedef Bool_t (*ThreadMsgFunc_t)(MSG*);

private:
   struct group     *fGroups;           // Groups on local computer
   struct passwd    *fPasswords;        // Users on local computer
   int               fNbUsers;          // Number of users on local computer
   int               fNbGroups;         // Number of groups on local computer
   int               fActUser;          // Index of actual user in User list
   Bool_t            fGroupsInitDone;   // Flag used for Users and Groups initialization
   Bool_t            fFirstFile;        // Flag used by OpenDirectory/GetDirEntry

   HANDLE            fhProcess;         // Handle of the current process
   char             *fDirNameBuffer;    // The string buffer to hold path name
   WIN32_FIND_DATA   fFindFileData;     // Structure to look for files (aka OpenDir under UNIX)

   Bool_t            CheckDescriptors();
   Bool_t            CheckSignals(Bool_t sync);
   Bool_t            CountMembers(const char *lpszGroupName);
   const char       *GetLinkedLibraries();
   Bool_t            GetNbGroups();
   Long_t            LookupSID (const char *lpszAccountName, int what, int &groupIdx, int &memberIdx);
   Bool_t            CollectMembers(const char *lpszGroupName, int &groupIdx, int &memberIdx);
   Bool_t            CollectGroups();
   Bool_t            InitUsersGroups();
   void              DoBeep(Int_t freq=-1, Int_t duration=-1) const;

   void              FillWithHomeDirectory(const char *userName, char *mydir) const;
   char             *GetWorkingDirectory(char driveletter) const;
   

protected:
   static int        WinNTUnixConnect(int port);
   static int        WinNTUnixConnect(const char *path);
   static int        WinNTUdpConnect(const char *hostname, int port);

public:
   TWinNTSystem();
   virtual ~TWinNTSystem();

   //---- Misc -------------------------------------------------
   Bool_t            Init();
   const char       *BaseName(const char *name);
   void              SetProgname(const char *name);
   const char       *GetError();
   const char       *HostName();
   void              NotifyApplicationCreated();

   //---- Handling of system events ----------------------------
   void              DispatchSignals(ESignals sig);
   void              AddSignalHandler(TSignalHandler *sh);
   TSignalHandler   *RemoveSignalHandler(TSignalHandler *sh);
   void              ResetSignal(ESignals sig, Bool_t reset = kTRUE);
   void              ResetSignals();
   void              IgnoreSignal(ESignals sig, Bool_t ignore = kTRUE);
   void              StackTrace();

   //---- Floating Point Exceptions Control --------------------
   Int_t             GetFPEMask();
   Int_t             SetFPEMask(Int_t mask = kDefaultMask);

   //---- Processes --------------------------------------------
   int               Exec(const char *shellcmd);
   FILE             *OpenPipe(const char *shellcmd, const char *mode);
   int               ClosePipe(FILE *pipe);
   void              Exit(int code, Bool_t mode = kTRUE);
   void              Abort(int code = 0);
   int               GetPid();

   //---- Environment manipulation -----------------------------
   const char       *Getenv(const char *name);
   void              Setenv(const char *name, const char *value);

   //---- Directories ------------------------------------------
   int               mkdir(const char *name, Bool_t recursive = kFALSE);
   int               MakeDirectory(const char *name);
   Bool_t            ChangeDirectory(const char *path);
   const char       *GetDirEntry(void *dirp);
   const char       *DirName(const char *pathname);
   void              FreeDirectory(void *dirp);
   void             *OpenDirectory(const char *name);
   const char       *WorkingDirectory(char driveletter);
   const char       *WorkingDirectory();
   std::string       GetWorkingDirectory() const;
   const char       *HomeDirectory(const char *userName=0);
   std::string       GetHomeDirectory(const char *userName=0) const;
   const char       *TempDirectory() const;
   FILE             *TempFileName(TString &base, const char *dir = 0);

   //---- Users & Groups ---------------------------------------
   Int_t             GetUid(const char *user = 0);
   Int_t             GetGid(const char *group = 0);
   Int_t             GetEffectiveUid();
   Int_t             GetEffectiveGid();
   UserGroup_t      *GetUserInfo(Int_t uid);
   UserGroup_t      *GetUserInfo(const char *user = 0);
   UserGroup_t      *GetGroupInfo(Int_t gid);
   UserGroup_t      *GetGroupInfo(const char *group = 0);

   //---- Paths & Files ----------------------------------------
   const char        DriveName(const char *pathname="/");
   const char       *PrependPathName(const char *dir, TString& name);
   Bool_t            ExpandPathName(TString &patbuf);
   char             *ExpandPathName(const char *path);
   Bool_t            AccessPathName(const char *path, EAccessMode mode = kFileExists);
   Bool_t            IsPathLocal(const char *path);
   Bool_t            IsAbsoluteFileName(const char *dir);
   int               CopyFile(const char *from, const char *to, Bool_t overwrite = kFALSE);
   int               Rename(const char *from, const char *to);
   int               Link(const char *from, const char *to);
   int               Symlink(const char *from, const char *to);
   int               Unlink(const char *name);
   int               SetNonBlock(int fd);
   int               GetPathInfo(const char *path, FileStat_t &buf);
   int               GetFsInfo(const char *path, Long_t *id, Long_t *bsize,
                                 Long_t *blocks, Long_t *bfree);
   int               Chmod(const char *file, UInt_t mode);
   int               Umask(Int_t mask);
   int               Utime(const char *file, Long_t modtime, Long_t actime);
   const char       *UnixPathName(const char *unixpathname);
   const char       *FindFile(const char *search, TString& file, EAccessMode mode = kFileExists);
   TList            *GetVolumes(Option_t *opt = "") const;

   //---- Standard Output redirection --------------------------
   Int_t             RedirectOutput(const char *name, const char *mode = "a", RedirectHandle_t *h = 0);

   //---- Dynamic Loading --------------------------------------
   void              AddDynamicPath(const char *dir);
   const char       *GetDynamicPath();
   void              SetDynamicPath(const char *path);
   const char       *FindDynamicLibrary(TString &lib, Bool_t quiet = kFALSE);
   int               Load(const char *module, const char *entry = "", Bool_t system = kFALSE);
   const char       *GetLibraries(const char *regexp = "",
                                  const char *option = "",
                                  Bool_t isRegexp = kTRUE);

   //---- Time & Date -------------------------------------------
   TTime             Now();
   Double_t          GetRealTime();
   Double_t          GetCPUTime();

   //---- RPC --------------------------------------------------
   TInetAddress      GetHostByName(const char *server);

   ClassDef(TWinNTSystem, 0)
};

} // namespace CppyyLegacy

#endif
