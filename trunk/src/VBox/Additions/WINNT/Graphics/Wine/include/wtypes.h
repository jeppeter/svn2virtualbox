/*** Autogenerated by WIDL 1.1.30 from ../../include/wtypes.idl - Do not edit ***/

#include <rpc.h>
#include <rpcndr.h>

#ifndef __WIDL_WTYPES_H
#define __WIDL_WTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Headers for imported files */

#include <basetsd.h>
#include <guiddef.h>

/* Forward declarations */


void * __RPC_USER MIDL_user_allocate(SIZE_T);
void __RPC_USER MIDL_user_free(void *);

/*****************************************************************************
 * IWinTypes interface (v0.1)
 */
#ifndef __IWinTypes_INTERFACE_DEFINED__
#define __IWinTypes_INTERFACE_DEFINED__

extern RPC_IF_HANDLE IWinTypes_v0_1_c_ifspec;
extern RPC_IF_HANDLE IWinTypes_v0_1_s_ifspec;
#if 0 /* winnt.h */
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef ULONG DWORD;
typedef LONG BOOL;
typedef unsigned char UCHAR;
typedef int INT;
typedef unsigned int UINT;
typedef short SHORT;
typedef unsigned short USHORT;
typedef LONG LONG;
typedef ULONG ULONG;
typedef float FLOAT;
typedef void *PVOID;
typedef void *LPVOID;
typedef DWORD *LPDWORD;
typedef char CHAR;
typedef CHAR *LPSTR;
typedef const CHAR *LPCSTR;
typedef wchar_t WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef boolean BOOLEAN;
typedef DWORD COLORREF;
typedef void *HANDLE;
typedef void *HMODULE;
typedef void *HINSTANCE;
typedef void *HRGN;
typedef void *HTASK;
typedef void *HKEY;
typedef void *HDESK;
typedef void *HMF;
typedef void *HEMF;
typedef void *HPEN;
typedef void *HRSRC;
typedef void *HSTR;
typedef void *HWINSTA;
typedef void *HKL;
typedef void *HGDIOBJ;
typedef HANDLE HDWP;
typedef LONG_PTR LRESULT;
typedef LONG HRESULT;
typedef DWORD LCID;
typedef USHORT LANGID;
typedef unsigned __int64 DWORDLONG;
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;
typedef struct _LARGE_INTEGER {
    LONGLONG QuadPart;
} LARGE_INTEGER;
typedef struct _ULARGE_INTEGER {
    ULONGLONG QuadPart;
} ULARGE_INTEGER;
typedef struct _SID_IDENTIFIER_AUTHORITY {
    UCHAR Value[6];
} SID_IDENTIFIER_AUTHORITY;
typedef struct _SID_IDENTIFIER_AUTHORITY *PSID_IDENTIFIER_AUTHORITY;
typedef struct _SID {
    UCHAR Revision;
    UCHAR SubAuthorityCount;
    SID_IDENTIFIER_AUTHORITY IdentifierAuthority;
    ULONG SubAuthority[1];
} SID;
typedef struct _SID *PSID;
typedef USHORT SECURITY_DESCRIPTOR_CONTROL;
typedef USHORT *PSECURITY_DESCRIPTOR_CONTROL;
typedef struct _ACL {
    UCHAR AclRevision;
    UCHAR Sbz1;
    USHORT AclSize;
    USHORT AceCount;
    USHORT Sbz2;
} ACL;
typedef ACL *PACL;
typedef struct _SECURITY_DESCRIPTOR {
    UCHAR Revision;
    UCHAR Sbz1;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSID Owner;
    PSID Group;
    PACL Sacl;
    PACL Dacl;
} SECURITY_DESCRIPTOR;
typedef struct _SECURITY_DESCRIPTOR *PSECURITY_DESCRIPTOR;
typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;
typedef struct _SECURITY_ATTRIBUTES *PSECURITY_ATTRIBUTES;
typedef struct _SECURITY_ATTRIBUTES *LPSECURITY_ATTRIBUTES;
typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;
typedef struct tagSIZE *PSIZE;
typedef struct tagSIZE *LPSIZE;
typedef SIZE SIZEL;
typedef SIZE *PSIZEL;
typedef SIZE *LPSIZEL;
typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;
typedef struct tagPOINT *PPOINT;
typedef struct tagPOINT *LPPOINT;
typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL;
typedef struct _POINTL *PPOINTL;
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
typedef struct tagRECT *PRECT;
typedef struct tagRECT *LPRECT;
typedef const RECT *LPCRECT;
typedef struct _RECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL;
typedef struct _RECTL *PRECTL;
typedef struct _RECTL *LPRECTL;
typedef const RECTL *LPCRECTL;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
#endif /* winnt.h */
#ifdef _MSC_VER
typedef double DOUBLE;
#else
typedef double DECLSPEC_ALIGN(8) DOUBLE;
#endif
#ifndef _PALETTEENTRY_DEFINED
#define _PALETTEENTRY_DEFINED
typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY;
typedef struct tagPALETTEENTRY *PPALETTEENTRY;
typedef struct tagPALETTEENTRY *LPPALETTEENTRY;
#endif
#ifndef _LOGPALETTE_DEFINED
#define _LOGPALETTE_DEFINED
typedef struct tagLOGPALETTE {
    WORD palVersion;
    WORD palNumEntries;
    PALETTEENTRY palPalEntry[1];
} LOGPALETTE;
typedef struct tagLOGPALETTE *PLOGPALETTE;
typedef struct tagLOGPALETTE *LPLOGPALETTE;
#endif
#ifndef _SYSTEMTIME_
#define _SYSTEMTIME_
typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;
typedef struct _SYSTEMTIME *PSYSTEMTIME;
typedef struct _SYSTEMTIME *LPSYSTEMTIME;
#endif
#ifndef _FILETIME_
#define _FILETIME_
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;
typedef struct _FILETIME *PFILETIME;
typedef struct _FILETIME *LPFILETIME;
#endif
#ifndef _TEXTMETRIC_DEFINED
#define _TEXTMETRIC_DEFINED
typedef struct tagTEXTMETRICA {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA;
typedef struct tagTEXTMETRICA *LPTEXTMETRICA;
typedef struct tagTEXTMETRICA *PTEXTMETRICA;
typedef struct tagTEXTMETRICW {
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    WCHAR tmFirstChar;
    WCHAR tmLastChar;
    WCHAR tmDefaultChar;
    WCHAR tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICW;
typedef struct tagTEXTMETRICW *LPTEXTMETRICW;
typedef struct tagTEXTMETRICW *PTEXTMETRICW;
#endif
typedef WCHAR OLECHAR;
typedef OLECHAR *LPOLESTR;
typedef const OLECHAR *LPCOLESTR;
#ifndef __WINESRC__
#define OLESTR(str) L##str
#endif
typedef LONG SCODE;
typedef struct _COAUTHIDENTITY {
    USHORT *User;
    ULONG UserLength;
    USHORT *Domain;
    ULONG DomainLength;
    USHORT *Password;
    ULONG PasswordLength;
    ULONG Flags;
} COAUTHIDENTITY;
typedef struct _COAUTHINFO {
    DWORD dwAuthnSvc;
    DWORD dwAuthzSvc;
    LPWSTR pwszServerPrincName;
    DWORD dwAuthnLevel;
    DWORD dwImpersonationLevel;
    COAUTHIDENTITY *pAuthIdentityData;
    DWORD dwCapabilities;
} COAUTHINFO;
typedef enum tagMEMCTX {
    MEMCTX_TASK = 1,
    MEMCTX_SHARED = 2,
    MEMCTX_MACSYSTEM = 3,
    MEMCTX_UNKNOWN = -1,
    MEMCTX_SAME = -2
} MEMCTX;
#ifndef _ROT_COMPARE_MAX_DEFINED
#define _ROT_COMPARE_MAX_DEFINED
#define ROT_COMPARE_MAX 2048
#endif
#ifndef _ROTFLAGS_DEFINED
#define _ROTFLAGS_DEFINED
#define ROTFLAGS_REGISTRATIONKEEPSALIVE 0x1
#define ROTFLAGS_ALLOWANYCLIENT 0x2
#endif
typedef enum tagCLSCTX {
    CLSCTX_INPROC_SERVER = 0x1,
    CLSCTX_INPROC_HANDLER = 0x2,
    CLSCTX_LOCAL_SERVER = 0x4,
    CLSCTX_INPROC_SERVER16 = 0x8,
    CLSCTX_REMOTE_SERVER = 0x10,
    CLSCTX_INPROC_HANDLER16 = 0x20,
    CLSCTX_INPROC_SERVERX86 = 0x40,
    CLSCTX_INPROC_HANDLERX86 = 0x80,
    CLSCTX_ESERVER_HANDLER = 0x100,
    CLSCTX_NO_CODE_DOWNLOAD = 0x400,
    CLSCTX_NO_CUSTOM_MARSHAL = 0x1000,
    CLSCTX_ENABLE_CODE_DOWNLOAD = 0x2000,
    CLSCTX_NO_FAILURE_LOG = 0x4000,
    CLSCTX_DISABLE_AAA = 0x8000,
    CLSCTX_ENABLE_AAA = 0x10000,
    CLSCTX_FROM_DEFAULT_CONTEXT = 0x20000
} CLSCTX;
#define CLSCTX_INPROC (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER)
#define CLSCTX_ALL (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
#define CLSCTX_SERVER (CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
typedef enum tagMSHLFLAGS {
    MSHLFLAGS_NORMAL = 0,
    MSHLFLAGS_TABLESTRONG = 1,
    MSHLFLAGS_TABLEWEAK = 2,
    MSHLFLAGS_NOPING = 4
} MSHLFLAGS;
typedef enum tagMSHCTX {
    MSHCTX_LOCAL = 0,
    MSHCTX_NOSHAREDMEM = 1,
    MSHCTX_DIFFERENTMACHINE = 2,
    MSHCTX_INPROC = 3,
    MSHCTX_CROSSCTX = 4
} MSHCTX;
typedef struct _BYTE_BLOB {
    ULONG clSize;
    byte abData[1];
} BYTE_BLOB;
typedef BYTE_BLOB *UP_BYTE_BLOB;
typedef struct _FLAGGED_BYTE_BLOB {
    ULONG fFlags;
    ULONG clSize;
    byte abData[1];
} FLAGGED_BYTE_BLOB;
typedef FLAGGED_BYTE_BLOB *UP_FLAGGED_BYTE_BLOB;
typedef struct _FLAGGED_WORD_BLOB {
    ULONG fFlags;
    ULONG clSize;
    unsigned short asData[1];
} FLAGGED_WORD_BLOB;
typedef FLAGGED_WORD_BLOB *UP_FLAGGED_WORD_BLOB;
typedef struct _BYTE_SIZEDARR {
    ULONG clSize;
    byte *pData;
} BYTE_SIZEDARR;
typedef struct _SHORT_SIZEDARR {
    ULONG clSize;
    unsigned short *pData;
} WORD_SIZEDARR;
typedef struct _LONG_SIZEDARR {
    ULONG clSize;
    ULONG *pData;
} DWORD_SIZEDARR;
typedef struct _HYPER_SIZEDARR {
    ULONG clSize;
    hyper *pData;
} HYPER_SIZEDARR;
#define WDT_INPROC_CALL (0x48746457)

#define WDT_REMOTE_CALL (0x52746457)

#define WDT_INPROC64_CALL (0x50746457)

typedef struct _userCLIPFORMAT {
    LONG fContext;
    union {
        DWORD dwValue;
        LPWSTR pwszName;
    } u;
} userCLIPFORMAT;
typedef userCLIPFORMAT *wireCLIPFORMAT;
typedef WORD CLIPFORMAT;
typedef struct tagRemHGLOBAL {
    LONG fNullHGlobal;
    ULONG cbData;
    byte data[1];
} RemHGLOBAL;
typedef struct _userHGLOBAL {
    LONG fContext;
    union {
        LONG hInproc;
        FLAGGED_BYTE_BLOB *hRemote;
        __int64 hInproc64;
    } u;
} userHGLOBAL;
typedef userHGLOBAL *wireHGLOBAL;
typedef struct tagRemHMETAFILEPICT {
    LONG mm;
    LONG xExt;
    LONG yExt;
    ULONG cbData;
    byte data[1];
} RemHMETAFILEPICT;
typedef struct _userHMETAFILE {
    LONG fContext;
    union {
        LONG hInproc;
        BYTE_BLOB *hRemote;
        __int64 hInproc64;
    } u;
} userHMETAFILE;
typedef userHMETAFILE *wireHMETAFILE;
typedef struct _remoteMETAFILEPICT {
    LONG mm;
    LONG xExt;
    LONG yExt;
    userHMETAFILE *hMF;
} remoteMETAFILEPICT;
typedef struct _userHMETAFILEPICT {
    LONG fContext;
    union {
        LONG hInproc;
        remoteMETAFILEPICT *hRemote;
        __int64 hInproc64;
    } u;
} userHMETAFILEPICT;
typedef userHMETAFILEPICT *wireHMETAFILEPICT;
typedef struct tagRemHENHMETAFILE {
    ULONG cbData;
    byte data[1];
} RemHENHMETAFILE;
typedef struct _userHENHMETAFILE {
    LONG fContext;
    union {
        LONG hInproc;
        BYTE_BLOB *hRemote;
        __int64 hInproc64;
    } u;
} userHENHMETAFILE;
typedef userHENHMETAFILE *wireHENHMETAFILE;
typedef struct tagRemHBITMAP {
    ULONG cbData;
    byte data[1];
} RemHBITMAP;
typedef struct _userBITMAP {
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    ULONG cbSize;
    byte pBuffer[1];
} userBITMAP;
typedef struct _userHBITMAP {
    LONG fContext;
    union {
        LONG hInproc;
        userBITMAP *hRemote;
        __int64 hInproc64;
    } u;
} userHBITMAP;
typedef userHBITMAP *wireHBITMAP;
typedef struct tagRemHPALETTE {
    ULONG cbData;
    byte data[1];
} RemHPALETTE;
typedef struct tagrpcLOGPALETTE {
    WORD palVersion;
    WORD palNumEntries;
    PALETTEENTRY palPalEntry[1];
} rpcLOGPALETTE;
typedef struct _userHPALETTE {
    LONG fContext;
    union {
        LONG hInproc;
        rpcLOGPALETTE *hRemote;
        __int64 hInproc64;
    } u;
} userHPALETTE;
typedef userHPALETTE *wireHPALETTE;
#if 0
typedef void *HGLOBAL;
typedef HGLOBAL HLOCAL;
typedef void *HBITMAP;
typedef void *HPALETTE;
typedef void *HENHMETAFILE;
typedef void *HMETAFILE;
#endif
typedef void *HMETAFILEPICT;
typedef struct _RemotableHandle {
    LONG fContext;
    union {
        LONG hInproc;
        LONG hRemote;
    } u;
} RemotableHandle;
typedef RemotableHandle *wireHACCEL;
typedef RemotableHandle *wireHBRUSH;
typedef RemotableHandle *wireHDC;
typedef RemotableHandle *wireHFONT;
typedef RemotableHandle *wireHICON;
typedef RemotableHandle *wireHMENU;
typedef RemotableHandle *wireHWND;
#if 0
typedef void *HACCEL;
typedef void *HBRUSH;
typedef void *HDC;
typedef void *HFONT;
typedef void *HICON;
typedef void *HMENU;
typedef void *HWND;
typedef HICON HCURSOR;
typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;
typedef struct tagMSG *PMSG;
typedef struct tagMSG *NPMSG;
typedef struct tagMSG *LPMSG;
#endif
#if 0
typedef GUID *REFGUID;
typedef IID *REFIID;
typedef CLSID *REFCLSID;
typedef FMTID *REFFMTID;
#endif
typedef enum tagDVASPECT {
    DVASPECT_CONTENT = 1,
    DVASPECT_THUMBNAIL = 2,
    DVASPECT_ICON = 4,
    DVASPECT_DOCPRINT = 8
} DVASPECT;
typedef enum tagSTGC {
    STGC_DEFAULT = 0,
    STGC_OVERWRITE = 1,
    STGC_ONLYIFCURRENT = 2,
    STGC_DANGEROUSLYCOMMITMERELYTODISKCACHE = 4,
    STGC_CONSOLIDATE = 8
} STGC;
typedef enum tagSTGMOVE {
    STGMOVE_MOVE = 0,
    STGMOVE_COPY = 1,
    STGMOVE_SHALLOWCOPY = 2
} STGMOVE;
typedef enum tagSTATFLAG {
    STATFLAG_DEFAULT = 0,
    STATFLAG_NONAME = 1,
    STATFLAG_NOOPEN = 2
} STATFLAG;
#ifdef _MSC_VER
typedef double DATE;
#else
typedef double DECLSPEC_ALIGN(8) DATE;
#endif
#if 0
typedef struct tagCY {
    LONGLONG int64;
} CY;
#else
#ifndef _tagCY_DEFINED
#define _tagCY_DEFINED
typedef union tagCY {
    struct {
#ifdef WORDS_BIGENDIAN
        LONG  Hi;
        ULONG Lo;
#else
        ULONG Lo;
        LONG  Hi;
#endif
    } DUMMYSTRUCTNAME;
    LONGLONG int64;
} CY;
#endif
#endif
typedef CY *LPCY;
#if 0
typedef struct tagDEC {
    USHORT wReserved;
    BYTE scale;
    BYTE sign;
    ULONG Hi32;
    ULONGLONG Lo64;
} DECIMAL;
#else
typedef struct tagDEC {
  USHORT wReserved;
  union {
    struct {
      BYTE scale;
      BYTE sign;
    } DUMMYSTRUCTNAME;
    USHORT signscale;
  } DUMMYUNIONNAME;
  ULONG Hi32;
  union {
    struct {
#ifdef WORDS_BIGENDIAN
      ULONG Mid32;
      ULONG Lo32;
#else
      ULONG Lo32;
      ULONG Mid32;
#endif
    } DUMMYSTRUCTNAME1;
    ULONGLONG Lo64;
  } DUMMYUNIONNAME1;
} DECIMAL;
#endif
#define DECIMAL_NEG ((BYTE)0x80)
#define DECIMAL_SETZERO(d) do{ memset(((char*)&(d)) + sizeof(USHORT), 0, sizeof(ULONG) * 3u + sizeof(USHORT)); }while (0)
typedef DECIMAL *LPDECIMAL;
typedef FLAGGED_WORD_BLOB *wireBSTR;
typedef OLECHAR *BSTR;
typedef BSTR *LPBSTR;
typedef short VARIANT_BOOL;
typedef VARIANT_BOOL _VARIANT_BOOL;
#define VARIANT_TRUE  ((VARIANT_BOOL)0xFFFF)
#define VARIANT_FALSE ((VARIANT_BOOL)0x0000)
typedef struct tagBSTRBLOB {
    ULONG cbSize;
    BYTE *pData;
} BSTRBLOB;
typedef struct tagBSTRBLOB *LPBSTRBLOB;
#ifndef _tagBLOB_DEFINED
#define _tagBLOB_DEFINED
#define _BLOB_DEFINED
#define _LPBLOB_DEFINED
typedef struct tagBLOB {
    ULONG cbSize;
    BYTE *pBlobData;
} BLOB;
typedef struct tagBLOB *LPBLOB;
#endif
typedef struct tagCLIPDATA {
    ULONG cbSize;
    LONG ulClipFmt;
    BYTE *pClipData;
} CLIPDATA;
#define CBPCLIPDATA(cb) ((cb).cbSize - sizeof((cb).ulClipFmt))
typedef ULONG PROPID;
typedef unsigned short VARTYPE;
enum VARENUM {
    VT_EMPTY = 0,
    VT_NULL = 1,
    VT_I2 = 2,
    VT_I4 = 3,
    VT_R4 = 4,
    VT_R8 = 5,
    VT_CY = 6,
    VT_DATE = 7,
    VT_BSTR = 8,
    VT_DISPATCH = 9,
    VT_ERROR = 10,
    VT_BOOL = 11,
    VT_VARIANT = 12,
    VT_UNKNOWN = 13,
    VT_DECIMAL = 14,
    VT_I1 = 16,
    VT_UI1 = 17,
    VT_UI2 = 18,
    VT_UI4 = 19,
    VT_I8 = 20,
    VT_UI8 = 21,
    VT_INT = 22,
    VT_UINT = 23,
    VT_VOID = 24,
    VT_HRESULT = 25,
    VT_PTR = 26,
    VT_SAFEARRAY = 27,
    VT_CARRAY = 28,
    VT_USERDEFINED = 29,
    VT_LPSTR = 30,
    VT_LPWSTR = 31,
    VT_RECORD = 36,
    VT_INT_PTR = 37,
    VT_UINT_PTR = 38,
    VT_FILETIME = 64,
    VT_BLOB = 65,
    VT_STREAM = 66,
    VT_STORAGE = 67,
    VT_STREAMED_OBJECT = 68,
    VT_STORED_OBJECT = 69,
    VT_BLOB_OBJECT = 70,
    VT_CF = 71,
    VT_CLSID = 72,
    VT_VERSIONED_STREAM = 73,
    VT_BSTR_BLOB = 0xfff,
    VT_VECTOR = 0x1000,
    VT_ARRAY = 0x2000,
    VT_BYREF = 0x4000,
    VT_RESERVED = 0x8000,
    VT_ILLEGAL = 0xffff,
    VT_ILLEGALMASKED = 0xfff,
    VT_TYPEMASK = 0xfff
};

typedef struct tagCSPLATFORM {
    DWORD dwPlatformId;
    DWORD dwVersionHi;
    DWORD dwVersionLo;
    DWORD dwProcessorArch;
} CSPLATFORM;
typedef struct tagQUERYCONTEXT {
    DWORD dwContext;
    CSPLATFORM Platform;
    LCID Locale;
    DWORD dwVersionHi;
    DWORD dwVersionLo;
} QUERYCONTEXT;
typedef enum tagTYSPEC {
    TYSPEC_CLSID = 0,
    TYSPEC_FILEEXT = 1,
    TYSPEC_MIMETYPE = 2,
    TYSPEC_PROGID = 3,
    TYSPEC_FILENAME = 4,
    TYSPEC_PACKAGENAME = 5,
    TYSPEC_OBJECTID = 6
} TYSPEC;
typedef struct __WIDL_wtypes_generated_name_00000000 {
    DWORD tyspec;
    union {
        CLSID clsid;
        LPOLESTR pFileExt;
        LPOLESTR pMimeType;
        LPOLESTR pProgId;
        LPOLESTR pFileName;
        struct {
            LPOLESTR pPackageName;
            GUID PolicyId;
        } ByName;
        struct {
            GUID ObjectId;
            GUID PolicyId;
        } ByObjectId;
    } tagged_union;
} uCLSSPEC;

#endif  /* __IWinTypes_INTERFACE_DEFINED__ */

/* Begin additional prototypes for all interfaces */


/* End additional prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __WIDL_WTYPES_H */
