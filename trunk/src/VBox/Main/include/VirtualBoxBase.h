/** @file
 * VirtualBox COM base classes definition
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_VIRTUALBOXBASEIMPL
#define ____H_VIRTUALBOXBASEIMPL

#include <iprt/cdefs.h>
#include <iprt/thread.h>

#include <list>
#include <map>

#include "VBox/com/AutoLock.h"
#include "VBox/com/string.h"
#include "VBox/com/Guid.h"

#include "VBox/com/VirtualBox.h"

// avoid including VBox/settings.h and VBox/xml.h;
// only declare the classes
namespace xml
{
class File;
}

using namespace com;
using namespace util;

class AutoInitSpan;
class AutoUninitSpan;

class VirtualBox;
class Machine;
class Medium;
class Host;
typedef std::list< ComObjPtr<Medium> > MediaList;

////////////////////////////////////////////////////////////////////////////////
//
// COM helpers
//
////////////////////////////////////////////////////////////////////////////////

#if !defined (VBOX_WITH_XPCOM)

#include <atlcom.h>

/* use a special version of the singleton class factory,
 * see KB811591 in msdn for more info. */

#undef DECLARE_CLASSFACTORY_SINGLETON
#define DECLARE_CLASSFACTORY_SINGLETON(obj) DECLARE_CLASSFACTORY_EX(CMyComClassFactorySingleton<obj>)

template <class T>
class CMyComClassFactorySingleton : public CComClassFactory
{
public:
    CMyComClassFactorySingleton() : m_hrCreate(S_OK){}
    virtual ~CMyComClassFactorySingleton(){}
    // IClassFactory
    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj)
    {
        HRESULT hRes = E_POINTER;
        if (ppvObj != NULL)
        {
            *ppvObj = NULL;
            // Aggregation is not supported in singleton objects.
            ATLASSERT(pUnkOuter == NULL);
            if (pUnkOuter != NULL)
                hRes = CLASS_E_NOAGGREGATION;
            else
            {
                if (m_hrCreate == S_OK && m_spObj == NULL)
                {
                    Lock();
                    __try
                    {
                        // Fix:  The following If statement was moved inside the __try statement.
                        // Did another thread arrive here first?
                        if (m_hrCreate == S_OK && m_spObj == NULL)
                        {
                            // lock the module to indicate activity
                            // (necessary for the monitor shutdown thread to correctly
                            // terminate the module in case when CreateInstance() fails)
                            _pAtlModule->Lock();
                            CComObjectCached<T> *p;
                            m_hrCreate = CComObjectCached<T>::CreateInstance(&p);
                            if (SUCCEEDED(m_hrCreate))
                            {
                                m_hrCreate = p->QueryInterface(IID_IUnknown, (void**)&m_spObj);
                                if (FAILED(m_hrCreate))
                                {
                                    delete p;
                                }
                            }
                            _pAtlModule->Unlock();
                        }
                    }
                    __finally
                    {
                        Unlock();
                    }
                }
                if (m_hrCreate == S_OK)
                {
                    hRes = m_spObj->QueryInterface(riid, ppvObj);
                }
                else
                {
                    hRes = m_hrCreate;
                }
            }
        }
        return hRes;
    }
    HRESULT m_hrCreate;
    CComPtr<IUnknown> m_spObj;
};

#endif /* !defined (VBOX_WITH_XPCOM) */

////////////////////////////////////////////////////////////////////////////////
//
// Macros
//
////////////////////////////////////////////////////////////////////////////////

/**
 *  Special version of the Assert macro to be used within VirtualBoxBase
 *  subclasses that also inherit the VirtualBoxSupportErrorInfoImpl template.
 *
 *  In the debug build, this macro is equivalent to Assert.
 *  In the release build, this macro uses |setError(E_FAIL, ...)| to set the
 *  error info from the asserted expression.
 *
 *  @see VirtualBoxSupportErrorInfoImpl::setError
 *
 *  @param   expr    Expression which should be true.
 */
#if defined (DEBUG)
#define ComAssert(expr)    Assert(expr)
#else
#define ComAssert(expr)    \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            setError(E_FAIL, \
                     "Assertion failed: [%s] at '%s' (%d) in %s.\nPlease contact the product vendor!", \
                     #expr, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
    } while (0)
#endif

/**
 *  Special version of the AssertMsg macro to be used within VirtualBoxBase
 *  subclasses that also inherit the VirtualBoxSupportErrorInfoImpl template.
 *
 *  See ComAssert for more info.
 *
 *  @param   expr    Expression which should be true.
 *  @param   a       printf argument list (in parenthesis).
 */
#if defined (DEBUG)
#define ComAssertMsg(expr, a)  AssertMsg(expr, a)
#else
#define ComAssertMsg(expr, a)  \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            setError(E_FAIL, \
                     "Assertion failed: [%s] at '%s' (%d) in %s.\n%s.\nPlease contact the product vendor!", \
                     #expr, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
    } while (0)
#endif

/**
 *  Special version of the AssertRC macro to be used within VirtualBoxBase
 *  subclasses that also inherit the VirtualBoxSupportErrorInfoImpl template.
 *
 *  See ComAssert for more info.
 *
 * @param   vrc     VBox status code.
 */
#if defined (DEBUG)
#define ComAssertRC(vrc)    AssertRC(vrc)
#else
#define ComAssertRC(vrc)    ComAssertMsgRC(vrc, ("%Rra", vrc))
#endif

/**
 *  Special version of the AssertMsgRC macro to be used within VirtualBoxBase
 *  subclasses that also inherit the VirtualBoxSupportErrorInfoImpl template.
 *
 *  See ComAssert for more info.
 *
 *  @param   vrc    VBox status code.
 *  @param   msg    printf argument list (in parenthesis).
 */
#if defined (DEBUG)
#define ComAssertMsgRC(vrc, msg)    AssertMsgRC(vrc, msg)
#else
#define ComAssertMsgRC(vrc, msg)    ComAssertMsg(RT_SUCCESS(vrc), msg)
#endif

/**
 *  Special version of the AssertComRC macro to be used within VirtualBoxBase
 *  subclasses that also inherit the VirtualBoxSupportErrorInfoImpl template.
 *
 *  See ComAssert for more info.
 *
 *  @param rc   COM result code
 */
#if defined (DEBUG)
#define ComAssertComRC(rc)  AssertComRC(rc)
#else
#define ComAssertComRC(rc)  ComAssertMsg(SUCCEEDED(rc), ("COM RC = %Rhrc (0x%08X)", (rc), (rc)))
#endif


/** Special version of ComAssert that returns ret if expr fails */
#define ComAssertRet(expr, ret)             \
    do { ComAssert(expr); if (!(expr)) return (ret); } while (0)
/** Special version of ComAssertMsg that returns ret if expr fails */
#define ComAssertMsgRet(expr, a, ret)       \
    do { ComAssertMsg(expr, a); if (!(expr)) return (ret); } while (0)
/** Special version of ComAssertRC that returns ret if vrc does not succeed */
#define ComAssertRCRet(vrc, ret)            \
    do { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) return (ret); } while (0)
/** Special version of ComAssertComRC that returns ret if rc does not succeed */
#define ComAssertComRCRet(rc, ret)          \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) return (ret); } while (0)
/** Special version of ComAssertComRC that returns rc if rc does not succeed */
#define ComAssertComRCRetRC(rc)             \
    do { ComAssertComRC(rc); if (!SUCCEEDED(rc)) return (rc); } while (0)


/** Special version of ComAssert that evaluates eval and breaks if expr fails */
#define ComAssertBreak(expr, eval)                \
    if (1) { ComAssert(expr); if (!(expr)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertMsg that evaluates eval and breaks if expr fails */
#define ComAssertMsgBreak(expr, a, eval)          \
    if (1)  { ComAssertMsg(expr, a); if (!(expr)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertRC that evaluates eval and breaks if vrc does not succeed */
#define ComAssertRCBreak(vrc, eval)               \
    if (1)  { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertFailed that evaluates eval and breaks */
#define ComAssertFailedBreak(eval)                \
    if (1)  { ComAssertFailed(); { eval; break; } } else do {} while (0)
/** Special version of ComAssertMsgFailed that evaluates eval and breaks */
#define ComAssertMsgFailedBreak(msg, eval)        \
    if (1)  { ComAssertMsgFailed (msg); { eval; break; } } else do {} while (0)
/** Special version of ComAssertComRC that evaluates eval and breaks if rc does not succeed */
#define ComAssertComRCBreak(rc, eval)             \
    if (1)  { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { eval; break; } } else do {} while (0)
/** Special version of ComAssertComRC that just breaks if rc does not succeed */
#define ComAssertComRCBreakRC(rc)                 \
    if (1)  { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { break; } } else do {} while (0)


/** Special version of ComAssert that evaluates eval and throws it if expr fails */
#define ComAssertThrow(expr, eval)                \
    if (1) { ComAssert(expr); if (!(expr)) { throw (eval); } } else do {} while (0)
/** Special version of ComAssertRC that evaluates eval and throws it if vrc does not succeed */
#define ComAssertRCThrow(vrc, eval)               \
    if (1)  { ComAssertRC(vrc); if (!RT_SUCCESS(vrc)) { throw (eval); } } else do {} while (0)
/** Special version of ComAssertComRC that evaluates eval and throws it if rc does not succeed */
#define ComAssertComRCThrow(rc, eval)             \
    if (1)  { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { throw (eval); } } else do {} while (0)
/** Special version of ComAssertComRC that just throws rc if rc does not succeed */
#define ComAssertComRCThrowRC(rc)                 \
    if (1)  { ComAssertComRC(rc); if (!SUCCEEDED(rc)) { throw rc; } } else do {} while (0)

////////////////////////////////////////////////////////////////////////////////

/**
 * Checks that the pointer argument is not NULL and returns E_INVALIDARG +
 * extended error info on failure.
 * @param arg   Input pointer-type argument (strings, interface pointers...)
 */
#define CheckComArgNotNull(arg) \
    do { \
        if (RT_UNLIKELY((arg) == NULL)) \
            return setError(E_INVALIDARG, tr("Argument %s is NULL"), #arg); \
    } while (0)

/**
 * Checks that safe array argument is not NULL and returns E_INVALIDARG +
 * extended error info on failure.
 * @param arg   Input safe array argument (strings, interface pointers...)
 */
#define CheckComArgSafeArrayNotNull(arg) \
    do { \
        if (RT_UNLIKELY(ComSafeArrayInIsNull(arg))) \
            return setError(E_INVALIDARG, tr("Argument %s is NULL"), #arg); \
    } while (0)

/**
 * Checks that the string argument is not a NULL or empty string and returns
 * E_INVALIDARG + extended error info on failure.
 * @param arg   Input string argument (BSTR etc.).
 */
#define CheckComArgStrNotEmptyOrNull(arg) \
    do { \
        if (RT_UNLIKELY((arg) == NULL || *(arg) == '\0')) \
            return setError(E_INVALIDARG, \
                tr("Argument %s is empty or NULL"), #arg); \
    } while (0)

/**
 * Checks that the given expression (that must involve the argument) is true and
 * returns E_INVALIDARG + extended error info on failure.
 * @param arg   Argument.
 * @param expr  Expression to evaluate.
 */
#define CheckComArgExpr(arg, expr) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return setError(E_INVALIDARG, \
                tr("Argument %s is invalid (must be %s)"), #arg, #expr); \
    } while (0)

/**
 * Checks that the given expression (that must involve the argument) is true and
 * returns E_INVALIDARG + extended error info on failure. The error message must
 * be customized.
 * @param arg   Argument.
 * @param expr  Expression to evaluate.
 * @param msg   Parenthesized printf-like expression (must start with a verb,
 *              like "must be one of...", "is not within...").
 */
#define CheckComArgExprMsg(arg, expr, msg) \
    do { \
        if (RT_UNLIKELY(!(expr))) \
            return setError(E_INVALIDARG, tr ("Argument %s %s"), \
                            #arg, Utf8StrFmt msg .raw()); \
    } while (0)

/**
 * Checks that the given pointer to an output argument is valid and returns
 * E_POINTER + extended error info otherwise.
 * @param arg   Pointer argument.
 */
#define CheckComArgOutPointerValid(arg) \
    do { \
        if (RT_UNLIKELY(!VALID_PTR(arg))) \
            return setError(E_POINTER, \
                tr("Output argument %s points to invalid memory location (%p)"), \
                #arg, (void *) (arg)); \
    } while (0)

/**
 * Checks that the given pointer to an output safe array argument is valid and
 * returns E_POINTER + extended error info otherwise.
 * @param arg   Safe array argument.
 */
#define CheckComArgOutSafeArrayPointerValid(arg) \
    do { \
        if (RT_UNLIKELY(ComSafeArrayOutIsNull(arg))) \
            return setError(E_POINTER, \
                            tr("Output argument %s points to invalid memory location (%p)"), \
                            #arg, (void*)(arg)); \
    } while (0)

/**
 * Sets the extended error info and returns E_NOTIMPL.
 */
#define ReturnComNotImplemented() \
    do { \
        return setError(E_NOTIMPL, tr("Method %s is not implemented"), __FUNCTION__); \
    } while (0)

/**
 *  Declares an empty constructor and destructor for the given class.
 *  This is useful to prevent the compiler from generating the default
 *  ctor and dtor, which in turn allows to use forward class statements
 *  (instead of including their header files) when declaring data members of
 *  non-fundamental types with constructors (which are always called implicitly
 *  by constructors and by the destructor of the class).
 *
 *  This macro is to be placed within (the public section of) the class
 *  declaration. Its counterpart, DEFINE_EMPTY_CTOR_DTOR, must be placed
 *  somewhere in one of the translation units (usually .cpp source files).
 *
 *  @param      cls     class to declare a ctor and dtor for
 */
#define DECLARE_EMPTY_CTOR_DTOR(cls) cls(); ~cls();

/**
 *  Defines an empty constructor and destructor for the given class.
 *  See DECLARE_EMPTY_CTOR_DTOR for more info.
 */
#define DEFINE_EMPTY_CTOR_DTOR(cls) \
    cls::cls()  { /*empty*/ } \
    cls::~cls() { /*empty*/ }

/**
 *  A variant of 'throw' that hits a debug breakpoint first to make
 *  finding the actual thrower possible.
 */
#ifdef DEBUG
#define DebugBreakThrow(a) \
    do { \
        RTAssertDebugBreak(); \
        throw (a); \
} while (0)
#else
#define DebugBreakThrow(a) throw (a)
#endif

/**
 * Parent class of VirtualBoxBase which enables translation support (which
 * Main doesn't have yet, but this provides the tr() function which will one
 * day provide translations).
 *
 * This class sits in between Lockable and VirtualBoxBase only for the one
 * reason that the USBProxyService wants translation support but is not
 * implemented as a COM object, which VirtualBoxBase implies.
 */
class ATL_NO_VTABLE VirtualBoxTranslatable
    : public Lockable
{
public:

    /**
     * Placeholder method with which translations can one day be implemented
     * in Main. This gets called by the tr() function.
     * @param context
     * @param pcszSourceText
     * @param comment
     * @return
     */
    static const char *translate(const char *context,
                                 const char *pcszSourceText,
                                 const char *comment = 0)
    {
        NOREF(context);
        NOREF(comment);
        return pcszSourceText;
    }

    /**
     * Translates the given text string by calling translate() and passing
     * the name of the C class as the first argument ("context of
     * translation"). See VirtualBoxBase::translate() for more info.
     *
     * @param aSourceText   String to translate.
     * @param aComment      Comment to the string to resolve possible
     *                      ambiguities (NULL means no comment).
     *
     * @return Translated version of the source string in UTF-8 encoding, or
     *      the source string itself if the translation is not found in the
     *      specified context.
     */
    inline static const char *tr(const char *pcszSourceText,
                                 const char *aComment = NULL)
    {
        return VirtualBoxTranslatable::translate(NULL, // getComponentName(), eventually
                                                 pcszSourceText,
                                                 aComment);
    }
};

////////////////////////////////////////////////////////////////////////////////
//
// VirtualBoxBase
//
////////////////////////////////////////////////////////////////////////////////

#define VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface) \
    virtual const IID& getClassIID() const \
    { \
        return cls::getStaticClassIID(); \
    } \
    static const IID& getStaticClassIID() \
    { \
        return COM_IIDOF(iface); \
    } \
    virtual const char* getComponentName() const \
    { \
        return cls::getStaticComponentName(); \
    } \
    static const char* getStaticComponentName() \
    { \
        return #cls; \
    }

/**
 * VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT:
 * This macro must be used once in the declaration of any class derived
 * from VirtualBoxBase. It implements the pure virtual getClassIID() and
 * getComponentName() methods. If this macro is not present, instances
 * of a class derived from VirtualBoxBase cannot be instantiated.
 *
 * @param X The class name, e.g. "Class".
 * @param IX The interface name which this class implements, e.g. "IClass".
 */
#ifdef VBOX_WITH_XPCOM
  #define VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(cls, iface) \
    VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface)
#else // #ifdef VBOX_WITH_XPCOM
  #define VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(cls, iface) \
    VIRTUALBOXBASE_ADD_VIRTUAL_COMPONENT_METHODS(cls, iface) \
    STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid) \
    { \
        const _ATL_INTMAP_ENTRY* pEntries = cls::_GetEntries(); \
        Assert(pEntries); \
        if (!pEntries) \
            return S_FALSE; \
        BOOL bSupports = FALSE; \
        BOOL bISupportErrorInfoFound = FALSE; \
        while (pEntries->pFunc != NULL && !bSupports) \
        { \
            if (!bISupportErrorInfoFound) \
                bISupportErrorInfoFound = InlineIsEqualGUID(*(pEntries->piid), IID_ISupportErrorInfo); \
            else \
                bSupports = InlineIsEqualGUID(*(pEntries->piid), riid); \
            pEntries++; \
        } \
        Assert(bISupportErrorInfoFound); \
        return bSupports ? S_OK : S_FALSE; \
    }
#endif // #ifdef VBOX_WITH_XPCOM

/**
 * Abstract base class for all component classes implementing COM
 * interfaces of the VirtualBox COM library.
 *
 * Declares functionality that should be available in all components.
 *
 * Among the basic functionality implemented by this class is the primary object
 * state that indicates if the object is ready to serve the calls, and if not,
 * what stage it is currently at. Here is the primary state diagram:
 *
 *              +-------------------------------------------------------+
 *              |                                                       |
 *              |         (InitFailed) -----------------------+         |
 *              |              ^                              |         |
 *              v              |                              v         |
 *  [*] ---> NotReady ----> (InInit) -----> Ready -----> (InUninit) ----+
 *                     ^       |
 *                     |       v
 *                     |    Limited
 *                     |       |
 *                     +-------+
 *
 * The object is fully operational only when its state is Ready. The Limited
 * state means that only some vital part of the object is operational, and it
 * requires some sort of reinitialization to become fully operational. The
 * NotReady state means the object is basically dead: it either was not yet
 * initialized after creation at all, or was uninitialized and is waiting to be
 * destroyed when the last reference to it is released. All other states are
 * transitional.
 *
 * The NotReady->InInit->Ready, NotReady->InInit->Limited and
 * NotReady->InInit->InitFailed transition is done by the AutoInitSpan smart
 * class.
 *
 * The Limited->InInit->Ready, Limited->InInit->Limited and
 * Limited->InInit->InitFailed transition is done by the AutoReinitSpan smart
 * class.
 *
 * The Ready->InUninit->NotReady and InitFailed->InUninit->NotReady
 * transitions are done by the AutoUninitSpan smart class.
 *
 * In order to maintain the primary state integrity and declared functionality
 * all subclasses must:
 *
 * 1) Use the above Auto*Span classes to perform state transitions. See the
 *    individual class descriptions for details.
 *
 * 2) All public methods of subclasses (i.e. all methods that can be called
 *    directly, not only from within other methods of the subclass) must have a
 *    standard prolog as described in the AutoCaller and AutoLimitedCaller
 *    documentation. Alternatively, they must use addCaller()/releaseCaller()
 *    directly (and therefore have both the prolog and the epilog), but this is
 *    not recommended.
 */
class ATL_NO_VTABLE VirtualBoxBase
    : public VirtualBoxTranslatable,
      public CComObjectRootEx<CComMultiThreadModel>
#if !defined (VBOX_WITH_XPCOM)
    , public ISupportErrorInfo
#endif
{
public:
    enum State { NotReady, Ready, InInit, InUninit, InitFailed, Limited };

    VirtualBoxBase();
    virtual ~VirtualBoxBase();

    /**
     * Unintialization method.
     *
     * Must be called by all final implementations (component classes) when the
     * last reference to the object is released, before calling the destructor.
     *
     * This method is also automatically called by the uninit() method of this
     * object's parent if this object is a dependent child of a class derived
     * from VirtualBoxBaseWithChildren (see
     * VirtualBoxBaseWithChildren::addDependentChild).
     *
     * @note Never call this method the AutoCaller scope or after the
     *       #addCaller() call not paired by #releaseCaller() because it is a
     *       guaranteed deadlock. See AutoUninitSpan for details.
     */
    virtual void uninit()
    { }

    virtual HRESULT addCaller(State *aState = NULL,
                              bool aLimited = false);
    virtual void releaseCaller();

    /**
     * Adds a limited caller. This method is equivalent to doing
     * <tt>addCaller (aState, true)</tt>, but it is preferred because provides
     * better self-descriptiveness. See #addCaller() for more info.
     */
    HRESULT addLimitedCaller(State *aState = NULL)
    {
        return addCaller(aState, true /* aLimited */);
    }

    /**
     * Pure virtual method for simple run-time type identification without
     * having to enable C++ RTTI.
     *
     * This *must* be implemented by every subclass deriving from VirtualBoxBase;
     * use the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro to do that most easily.
     */
    virtual const IID& getClassIID() const = 0;

    /**
     * Pure virtual method for simple run-time type identification without
     * having to enable C++ RTTI.
     *
     * This *must* be implemented by every subclass deriving from VirtualBoxBase;
     * use the VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT macro to do that most easily.
     */
    virtual const char* getComponentName() const = 0;

    /**
     * Virtual method which determins the locking class to be used for validating
     * lock order with the standard member lock handle. This method is overridden
     * in a number of subclasses.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        return LOCKCLASS_OTHEROBJECT;
    }

    virtual RWLockHandle *lockHandle() const;

    /**
     * Returns a lock handle used to protect the primary state fields (used by
     * #addCaller(), AutoInitSpan, AutoUninitSpan, etc.). Only intended to be
     * used for similar purposes in subclasses. WARNING: NO any other locks may
     * be requested while holding this lock!
     */
    WriteLockHandle *stateLockHandle() { return &mStateLock; }

    static HRESULT setErrorInternal(HRESULT aResultCode,
                                    const GUID &aIID,
                                    const char *aComponent,
                                    const Utf8Str &aText,
                                    bool aWarning,
                                    bool aLogIt);

    HRESULT setError(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setWarning(HRESULT aResultCode, const char *pcsz, ...);
    HRESULT setErrorNoLog(HRESULT aResultCode, const char *pcsz, ...);

private:

    void setState(State aState)
    {
        Assert(mState != aState);
        mState = aState;
        mStateChangeThread = RTThreadSelf();
    }

    /** Primary state of this object */
    State mState;
    /** Thread that caused the last state change */
    RTTHREAD mStateChangeThread;
    /** Total number of active calls to this object */
    unsigned mCallers;
    /** Posted when the number of callers drops to zero */
    RTSEMEVENT mZeroCallersSem;
    /** Posted when the object goes from InInit/InUninit to some other state */
    RTSEMEVENTMULTI mInitUninitSem;
    /** Number of threads waiting for mInitUninitDoneSem */
    unsigned mInitUninitWaiters;

    /** Protects access to state related data members */
    WriteLockHandle mStateLock;

    /** User-level object lock for subclasses */
    mutable RWLockHandle *mObjectLock;

    friend class AutoInitSpan;
    friend class AutoReinitSpan;
    friend class AutoUninitSpan;
};

/**
 * Dummy macro that is used to shut down Qt's lupdate tool warnings in some
 * situations. This macro needs to be present inside (better at the very
 * beginning) of the declaration of the class that inherits from
 * VirtualBoxSupportTranslation template, to make lupdate happy.
 */
#define Q_OBJECT

////////////////////////////////////////////////////////////////////////////////

/**
 * Base class to track VirtualBoxBaseNEXT chlidren of the component.
 *
 * This class is a preferrable VirtualBoxBase replacement for components that
 * operate with collections of child components. It gives two useful
 * possibilities:
 *
 * <ol><li>
 *      Given an IUnknown instance, it's possible to quickly determine
 *      whether this instance represents a child object that belongs to the
 *      given component, and if so, get a valid VirtualBoxBase pointer to the
 *      child object. The returned pointer can be then safely casted to the
 *      actual class of the child object (to get access to its "internal"
 *      non-interface methods) provided that no other child components implement
 *      the same original COM interface IUnknown is queried from.
 * </li><li>
 *      When the parent object uninitializes itself, it can easily unintialize
 *      all its VirtualBoxBase derived children (using their
 *      VirtualBoxBase::uninit() implementations). This is done simply by
 *      calling the #uninitDependentChildren() method.
 * </li></ol>
 *
 * In order to let the above work, the following must be done:
 * <ol><li>
 *      When a child object is initialized, it calls #addDependentChild() of
 *      its parent to register itself within the list of dependent children.
 * </li><li>
 *      When the child object it is uninitialized, it calls
 *      #removeDependentChild() to unregister itself.
 * </li></ol>
 *
 * Note that if the parent object does not call #uninitDependentChildren() when
 * it gets uninitialized, it must call uninit() methods of individual children
 * manually to disconnect them; a failure to do so will cause crashes in these
 * methods when children get destroyed. The same applies to children not calling
 * #removeDependentChild() when getting destroyed.
 *
 * Note that children added by #addDependentChild() are <b>weakly</b> referenced
 * (i.e. AddRef() is not called), so when a child object is deleted externally
 * (because it's reference count goes to zero), it will automatically remove
 * itself from the map of dependent children provided that it follows the rules
 * described here.
 *
 * Access to the child list is serialized using the #childrenLock() lock handle
 * (which defaults to the general object lock handle (see
 * VirtualBoxBase::lockHandle()). This lock is used by all add/remove methods of
 * this class so be aware of the need to preserve the {parent, child} lock order
 * when calling these methods.
 *
 * Read individual method descriptions to get further information.
 *
 * @todo This is a VirtualBoxBaseWithChildren equivalent that uses the
 *       VirtualBoxBaseNEXT implementation. Will completely supersede
 *       VirtualBoxBaseWithChildren after the old VirtualBoxBase implementation
 *       has gone.
 */
class VirtualBoxBaseWithChildrenNEXT : public VirtualBoxBase
{
public:

    VirtualBoxBaseWithChildrenNEXT()
    {}

    virtual ~VirtualBoxBaseWithChildrenNEXT()
    {}

    /**
     * Lock handle to use when adding/removing child objects from the list of
     * children. It is guaranteed that no any other lock is requested in methods
     * of this class while holding this lock.
     *
     * @warning By default, this simply returns the general object's lock handle
     *          (see VirtualBoxBase::lockHandle()) which is sufficient for most
     *          cases.
     */
    virtual RWLockHandle *childrenLock() { return lockHandle(); }

    /**
     * Adds the given child to the list of dependent children.
     *
     * Usually gets called from the child's init() method.
     *
     * @note @a aChild (unless it is in InInit state) must be protected by
     *       VirtualBoxBase::AutoCaller to make sure it is not uninitialized on
     *       another thread during this method's call.
     *
     * @note When #childrenLock() is not overloaded (returns the general object
     *       lock) and this method is called from under the child's read or
     *       write lock, make sure the {parent, child} locking order is
     *       preserved by locking the callee (this object) for writing before
     *       the child's lock.
     *
     * @param aChild    Child object to add (must inherit VirtualBoxBase AND
     *                  implement some interface).
     *
     * @note Locks #childrenLock() for writing.
     */
    template<class C>
    void addDependentChild(C *aChild)
    {
        AssertReturnVoid(aChild != NULL);
        doAddDependentChild(ComPtr<IUnknown>(aChild), aChild);
    }

    /**
     * Equivalent to template <class C> void addDependentChild (C *aChild)
     * but takes a ComObjPtr<C> argument.
     */
    template<class C>
    void addDependentChild(const ComObjPtr<C> &aChild)
    {
        AssertReturnVoid(!aChild.isNull());
        doAddDependentChild(ComPtr<IUnknown>(static_cast<C *>(aChild)), aChild);
    }

    /**
     * Removes the given child from the list of dependent children.
     *
     * Usually gets called from the child's uninit() method.
     *
     * Keep in mind that the called (parent) object may be no longer available
     * (i.e. may be deleted deleted) after this method returns, so you must not
     * call any other parent's methods after that!
     *
     * @note Locks #childrenLock() for writing.
     *
     * @note @a aChild (unless it is in InUninit state) must be protected by
     *       VirtualBoxBase::AutoCaller to make sure it is not uninitialized on
     *       another thread during this method's call.
     *
     * @note When #childrenLock() is not overloaded (returns the general object
     *       lock) and this method is called from under the child's read or
     *       write lock, make sure the {parent, child} locking order is
     *       preserved by locking the callee (this object) for writing before
     *       the child's lock. This is irrelevant when the method is called from
     *       under this object's VirtualBoxBaseProto::AutoUninitSpan (i.e. in
     *       InUninit state) since in this case no locking is done.
     *
     * @param aChild    Child object to remove.
     *
     * @note Locks #childrenLock() for writing.
     */
    template<class C>
    void removeDependentChild(C *aChild)
    {
        AssertReturnVoid(aChild != NULL);
        doRemoveDependentChild(ComPtr<IUnknown>(aChild));
    }

    /**
     * Equivalent to template <class C> void removeDependentChild (C *aChild)
     * but takes a ComObjPtr<C> argument.
     */
    template<class C>
    void removeDependentChild(const ComObjPtr<C> &aChild)
    {
        AssertReturnVoid(!aChild.isNull());
        doRemoveDependentChild(ComPtr<IUnknown>(static_cast<C *>(aChild)));
    }

protected:

    void uninitDependentChildren();

    VirtualBoxBase *getDependentChild(const ComPtr<IUnknown> &aUnk);

private:
    void doAddDependentChild(IUnknown *aUnk, VirtualBoxBase *aChild);
    void doRemoveDependentChild(IUnknown *aUnk);

    typedef std::map<IUnknown*, VirtualBoxBase*> DependentChildren;
    DependentChildren mDependentChildren;
};

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////


/**
 *  Simple template that manages data structure allocation/deallocation
 *  and supports data pointer sharing (the instance that shares the pointer is
 *  not responsible for memory deallocation as opposed to the instance that
 *  owns it).
 */
template <class D>
class Shareable
{
public:

    Shareable() : mData (NULL), mIsShared(FALSE) {}
    ~Shareable() { free(); }

    void allocate() { attach(new D); }

    virtual void free() {
        if (mData) {
            if (!mIsShared)
                delete mData;
            mData = NULL;
            mIsShared = false;
        }
    }

    void attach(D *d) {
        AssertMsg(d, ("new data must not be NULL"));
        if (d && mData != d) {
            if (mData && !mIsShared)
                delete mData;
            mData = d;
            mIsShared = false;
        }
    }

    void attach(Shareable &d) {
        AssertMsg(
            d.mData == mData || !d.mIsShared,
            ("new data must not be shared")
        );
        if (this != &d && !d.mIsShared) {
            attach(d.mData);
            d.mIsShared = true;
        }
    }

    void share(D *d) {
        AssertMsg(d, ("new data must not be NULL"));
        if (mData != d) {
            if (mData && !mIsShared)
                delete mData;
            mData = d;
            mIsShared = true;
        }
    }

    void share(const Shareable &d) { share(d.mData); }

    void attachCopy(const D *d) {
        AssertMsg(d, ("data to copy must not be NULL"));
        if (d)
            attach(new D(*d));
    }

    void attachCopy(const Shareable &d) {
        attachCopy(d.mData);
    }

    virtual D *detach() {
        D *d = mData;
        mData = NULL;
        mIsShared = false;
        return d;
    }

    D *data() const {
        return mData;
    }

    D *operator->() const {
        AssertMsg(mData, ("data must not be NULL"));
        return mData;
    }

    bool isNull() const { return mData == NULL; }
    bool operator!() const { return isNull(); }

    bool isShared() const { return mIsShared; }

protected:

    D *mData;
    bool mIsShared;
};

/// @todo (dmik) remove after we switch to VirtualBoxBaseNEXT completely
/**
 *  Simple template that enhances Shareable<> and supports data
 *  backup/rollback/commit (using the copy constructor of the managed data
 *  structure).
 */
template<class D>
class Backupable : public Shareable<D>
{
public:

    Backupable() : Shareable<D> (), mBackupData(NULL) {}

    void free()
    {
        AssertMsg(this->mData || !mBackupData, ("backup must be NULL if data is NULL"));
        rollback();
        Shareable<D>::free();
    }

    D *detach()
    {
        AssertMsg(this->mData || !mBackupData, ("backup must be NULL if data is NULL"));
        rollback();
        return Shareable<D>::detach();
    }

    void share(const Backupable &d)
    {
        AssertMsg(!d.isBackedUp(), ("data to share must not be backed up"));
        if (!d.isBackedUp())
            Shareable<D>::share(d.mData);
    }

    /**
     *  Stores the current data pointer in the backup area, allocates new data
     *  using the copy constructor on current data and makes new data active.
     */
    void backup()
    {
        AssertMsg(this->mData, ("data must not be NULL"));
        if (this->mData && !mBackupData)
        {
            D *pNewData = new D(*this->mData);
            mBackupData = this->mData;
            this->mData = pNewData;
        }
    }

    /**
     *  Deletes new data created by #backup() and restores previous data pointer
     *  stored in the backup area, making it active again.
     */
    void rollback()
    {
        if (this->mData && mBackupData)
        {
            delete this->mData;
            this->mData = mBackupData;
            mBackupData = NULL;
        }
    }

    /**
     *  Commits current changes by deleting backed up data and clearing up the
     *  backup area. The new data pointer created by #backup() remains active
     *  and becomes the only managed pointer.
     *
     *  This method is much faster than #commitCopy() (just a single pointer
     *  assignment operation), but makes the previous data pointer invalid
     *  (because it is freed). For this reason, this method must not be
     *  used if it's possible that data managed by this instance is shared with
     *  some other Shareable instance. See #commitCopy().
     */
    void commit()
    {
        if (this->mData && mBackupData)
        {
            if (!this->mIsShared)
                delete mBackupData;
            mBackupData = NULL;
            this->mIsShared = false;
        }
    }

    /**
     *  Commits current changes by assigning new data to the previous data
     *  pointer stored in the backup area using the assignment operator.
     *  New data is deleted, the backup area is cleared and the previous data
     *  pointer becomes active and the only managed pointer.
     *
     *  This method is slower than #commit(), but it keeps the previous data
     *  pointer valid (i.e. new data is copied to the same memory location).
     *  For that reason it's safe to use this method on instances that share
     *  managed data with other Shareable instances.
     */
    void commitCopy()
    {
        if (this->mData && mBackupData)
        {
            *mBackupData = *(this->mData);
            delete this->mData;
            this->mData = mBackupData;
            mBackupData = NULL;
        }
    }

    void assignCopy(const D *pData)
    {
        AssertMsg(this->mData, ("data must not be NULL"));
        AssertMsg(pData, ("data to copy must not be NULL"));
        if (this->mData && pData)
        {
            if (!mBackupData)
            {
                D *pNewData = new D(*pData);
                mBackupData = this->mData;
                this->mData = pNewData;
            }
            else
                *this->mData = *pData;
        }
    }

    void assignCopy(const Backupable &d)
    {
        assignCopy(d.mData);
    }

    bool isBackedUp() const
    {
        return mBackupData != NULL;
    }

    D *backedUpData() const
    {
        return mBackupData;
    }

protected:

    D *mBackupData;
};

#endif // !____H_VIRTUALBOXBASEIMPL

