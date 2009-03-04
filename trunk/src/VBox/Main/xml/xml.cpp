/** @file
 * VirtualBox XML Manipulation API.
 */

/*
 * Copyright (C) 2007-2009 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include "Logging.h"

#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/lock.h>
#include <iprt/string.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/globals.h>
#include <libxml/xmlIO.h>
#include <libxml/xmlsave.h>
#include <libxml/uri.h>

#include <libxml/xmlschemas.h>

#include <list>
#include <map>
#include <boost/shared_ptr.hpp>

#include "VBox/xml.h"

////////////////////////////////////////////////////////////////////////////////
//
// globals
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Global module initialization structure. This is to wrap non-reentrant bits
 * of libxml, among other things.
 *
 * The constructor and destructor of this structure are used to perform global
 * module initiaizaton and cleanup. Thee must be only one global variable of
 * this structure.
 */
static
class Global
{
public:

    Global()
    {
        /* Check the parser version. The docs say it will kill the app if
         * there is a serious version mismatch, but I couldn't find it in the
         * source code (it only prints the error/warning message to the console) so
         * let's leave it as is for informational purposes. */
        LIBXML_TEST_VERSION

        /* Init libxml */
        xmlInitParser();

        /* Save the default entity resolver before someone has replaced it */
        sxml.defaultEntityLoader = xmlGetExternalEntityLoader();
    }

    ~Global()
    {
        /* Shutdown libxml */
        xmlCleanupParser();
    }

    struct
    {
        xmlExternalEntityLoader defaultEntityLoader;

        /** Used to provide some thread safety missing in libxml2 (see e.g.
         *  XmlTreeBackend::read()) */
        RTLockMtx lock;
    }
    sxml;  /* XXX naming this xml will break with gcc-3.3 */
}
gGlobal;



namespace xml
{

////////////////////////////////////////////////////////////////////////////////
//
// Exceptions
//
////////////////////////////////////////////////////////////////////////////////

LogicError::LogicError(RT_SRC_POS_DECL)
    : Error(NULL)
{
    char *msg = NULL;
    RTStrAPrintf(&msg, "In '%s', '%s' at #%d",
                 pszFunction, pszFile, iLine);
    setWhat(msg);
    RTStrFree(msg);
}

XmlError::XmlError(xmlErrorPtr aErr)
{
    if (!aErr)
        throw EInvalidArg(RT_SRC_POS);

    char *msg = Format(aErr);
    setWhat(msg);
    RTStrFree(msg);
}

/**
 * Composes a single message for the given error. The caller must free the
 * returned string using RTStrFree() when no more necessary.
 */
// static
char *XmlError::Format(xmlErrorPtr aErr)
{
    const char *msg = aErr->message ? aErr->message : "<none>";
    size_t msgLen = strlen(msg);
    /* strip spaces, trailing EOLs and dot-like char */
    while (msgLen && strchr(" \n.?!", msg [msgLen - 1]))
        --msgLen;

    char *finalMsg = NULL;
    RTStrAPrintf(&finalMsg, "%.*s.\nLocation: '%s', line %d (%d), column %d",
                 msgLen, msg, aErr->file, aErr->line, aErr->int1, aErr->int2);

    return finalMsg;
}

EIPRTFailure::EIPRTFailure(int aRC)
    : RuntimeError(NULL),
      mRC(aRC)
{
    char *newMsg = NULL;
    RTStrAPrintf(&newMsg, "Runtime error: %d (%s)", aRC, RTErrGetShort(aRC));
    setWhat(newMsg);
    RTStrFree(newMsg);
}

////////////////////////////////////////////////////////////////////////////////
//
// File Class
//
//////////////////////////////////////////////////////////////////////////////

struct File::Data
{
    Data()
        : fileName (NULL), handle (NIL_RTFILE), opened (false) {}

    char *fileName;
    RTFILE handle;
    bool opened : 1;
};

File::File(Mode aMode, const char *aFileName)
    : m (new Data())
{
    m->fileName = RTStrDup (aFileName);
    if (m->fileName == NULL)
        throw ENoMemory();

    unsigned flags = 0;
    switch (aMode)
    {
        case Mode_Read:
            flags = RTFILE_O_READ;
            break;
        case Mode_Write:
            flags = RTFILE_O_WRITE | RTFILE_O_CREATE;
            break;
        case Mode_ReadWrite:
            flags = RTFILE_O_READ | RTFILE_O_WRITE;
    }

    int vrc = RTFileOpen (&m->handle, aFileName, flags);
    if (RT_FAILURE (vrc))
        throw EIPRTFailure (vrc);

    m->opened = true;
}

File::File (RTFILE aHandle, const char *aFileName /* = NULL */)
    : m (new Data())
{
    if (aHandle == NIL_RTFILE)
        throw EInvalidArg (RT_SRC_POS);

    m->handle = aHandle;

    if (aFileName)
    {
        m->fileName = RTStrDup (aFileName);
        if (m->fileName == NULL)
            throw ENoMemory();
    }

    setPos (0);
}

File::~File()
{
    if (m->opened)
        RTFileClose (m->handle);

    RTStrFree (m->fileName);
}

const char *File::uri() const
{
    return m->fileName;
}

uint64_t File::pos() const
{
    uint64_t p = 0;
    int vrc = RTFileSeek (m->handle, 0, RTFILE_SEEK_CURRENT, &p);
    if (RT_SUCCESS (vrc))
        return p;

    throw EIPRTFailure (vrc);
}

void File::setPos (uint64_t aPos)
{
    uint64_t p = 0;
    unsigned method = RTFILE_SEEK_BEGIN;
    int vrc = VINF_SUCCESS;

    /* check if we overflow int64_t and move to INT64_MAX first */
    if (((int64_t) aPos) < 0)
    {
        vrc = RTFileSeek (m->handle, INT64_MAX, method, &p);
        aPos -= (uint64_t) INT64_MAX;
        method = RTFILE_SEEK_CURRENT;
    }
    /* seek the rest */
    if (RT_SUCCESS (vrc))
        vrc = RTFileSeek (m->handle, (int64_t) aPos, method, &p);
    if (RT_SUCCESS (vrc))
        return;

    throw EIPRTFailure (vrc);
}

int File::read (char *aBuf, int aLen)
{
    size_t len = aLen;
    int vrc = RTFileRead (m->handle, aBuf, len, &len);
    if (RT_SUCCESS (vrc))
        return len;

    throw EIPRTFailure (vrc);
}

int File::write (const char *aBuf, int aLen)
{
    size_t len = aLen;
    int vrc = RTFileWrite (m->handle, aBuf, len, &len);
    if (RT_SUCCESS (vrc))
        return len;

    throw EIPRTFailure (vrc);

    return -1 /* failure */;
}

void File::truncate()
{
    int vrc = RTFileSetSize (m->handle, pos());
    if (RT_SUCCESS (vrc))
        return;

    throw EIPRTFailure (vrc);
}

////////////////////////////////////////////////////////////////////////////////
//
// MemoryBuf Class
//
//////////////////////////////////////////////////////////////////////////////

struct MemoryBuf::Data
{
    Data()
        : buf (NULL), len (0), uri (NULL), pos (0) {}

    const char *buf;
    size_t len;
    char *uri;

    size_t pos;
};

MemoryBuf::MemoryBuf (const char *aBuf, size_t aLen, const char *aURI /* = NULL */)
    : m (new Data())
{
    if (aBuf == NULL)
        throw EInvalidArg (RT_SRC_POS);

    m->buf = aBuf;
    m->len = aLen;
    m->uri = RTStrDup (aURI);
}

MemoryBuf::~MemoryBuf()
{
    RTStrFree (m->uri);
}

const char *MemoryBuf::uri() const
{
    return m->uri;
}

uint64_t MemoryBuf::pos() const
{
    return m->pos;
}

void MemoryBuf::setPos (uint64_t aPos)
{
    size_t pos = (size_t) aPos;
    if ((uint64_t) pos != aPos)
        throw EInvalidArg();

    if (pos > m->len)
        throw EInvalidArg();

    m->pos = pos;
}

int MemoryBuf::read (char *aBuf, int aLen)
{
    if (m->pos >= m->len)
        return 0 /* nothing to read */;

    size_t len = m->pos + aLen < m->len ? aLen : m->len - m->pos;
    memcpy (aBuf, m->buf + m->pos, len);
    m->pos += len;

    return len;
}

////////////////////////////////////////////////////////////////////////////////
//
// GlobalLock class
//
////////////////////////////////////////////////////////////////////////////////

struct GlobalLock::Data
{
    PFNEXTERNALENTITYLOADER pOldLoader;
    RTLock lock;

    Data()
        : pOldLoader(NULL),
          lock(gGlobal.sxml.lock)
    {
    }
};

GlobalLock::GlobalLock()
    : m(new Data())
{
}

GlobalLock::~GlobalLock()
{
    if (m->pOldLoader)
        xmlSetExternalEntityLoader(m->pOldLoader);
}

void GlobalLock::setExternalEntityLoader(PFNEXTERNALENTITYLOADER pLoader)
{
    m->pOldLoader = xmlGetExternalEntityLoader();
    xmlSetExternalEntityLoader(pLoader);
}

// static
xmlParserInput* GlobalLock::callDefaultLoader(const char *aURI,
                                              const char *aID,
                                              xmlParserCtxt *aCtxt)
{
    return gGlobal.sxml.defaultEntityLoader(aURI, aID, aCtxt);
}

////////////////////////////////////////////////////////////////////////////////
//
// Node class
//
////////////////////////////////////////////////////////////////////////////////

struct Node::Data
{
    xmlNode     *plibNode;          // != NULL if this is an element
    xmlAttr     *plibAttr;          // != NULL if this is an attribute

    Node        *pParent;           // NULL only for the root element
    const char  *pcszName;          // points either into plibNode or plibAttr

    struct compare_const_char
    {
        bool operator()(const char* s1, const char* s2) const
        {
            return strcmp(s1, s2) < 0;
        }
    };

    // attributes, if this is an element; can be empty
    typedef std::map<const char*, boost::shared_ptr<Node>, compare_const_char > AttributesMap;
    AttributesMap attribs;

    // child elements, if this is an element; can be empty
    typedef std::list< boost::shared_ptr<Node> > InternalNodesList;
    InternalNodesList children;
};

Node::Node()
    : m(new Data)
{
    m->plibNode = NULL;
    m->plibAttr = NULL;
    m->pParent = NULL;
}

Node::~Node()
{
    delete m;
}

void Node::buildChildren()       // private
{
    // go thru this element's attributes
    xmlAttr *plibAttr = m->plibNode->properties;
    while (plibAttr)
    {
        const char *pcszAttribName = (const char*)plibAttr->name;
        boost::shared_ptr<Node> pNew(new Node);
        pNew->m->plibAttr = plibAttr;
        pNew->m->pcszName = (const char*)plibAttr->name;
        pNew->m->pParent = this;
        // store
        m->attribs[pcszAttribName] = pNew;

        plibAttr = plibAttr->next;
    }

    // go thru this element's child elements
    xmlNodePtr plibNode = m->plibNode->children;
    while (plibNode)
    {
        // create a new Node for this child element
        boost::shared_ptr<Node> pNew(new Node);
        pNew->m->plibNode = plibNode;
        pNew->m->pcszName = (const char*)plibNode->name;
        pNew->m->pParent = this;
        // store
        m->children.push_back(pNew);

        // recurse for this child element to get its own children
        pNew->buildChildren();

        plibNode = plibNode->next;
    }
}

const char* Node::getName() const
{
    return m->pcszName;
}

/**
 * Returns the value of a node. If this node is an attribute, returns
 * the attribute value; if this node is an element, then this returns
 * the element text content.
 * @return
 */
const char* Node::getValue() const
{
    if (    (m->plibAttr)
         && (m->plibAttr->children)
       )
        // libxml hides attribute values in another node created as a
        // single child of the attribute node, and it's in the content field
        return (const char*)m->plibAttr->children->content;

    if (    (m->plibNode)
         && (m->plibNode->children)
       )
        return (const char*)m->plibNode->children->content;

    return NULL;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(int32_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToInt32Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(uint32_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToUInt32Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(int64_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToInt64Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Copies the value of a node into the given integer variable.
 * Returns TRUE only if a value was found and was actually an
 * integer of the given type.
 * @return
 */
bool Node::copyValue(uint64_t &i) const
{
    const char *pcsz;
    if (    ((pcsz = getValue()))
         && (VINF_SUCCESS == RTStrToUInt64Ex(pcsz, NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Returns the line number of the current node in the source XML file.
 * Useful for error messages.
 * @return
 */
int Node::getLineNumber() const
{
    if (m->plibAttr)
        return m->pParent->m->plibNode->line;

    return m->plibNode->line;
}

/**
 * Builds a list of direct child elements of the current element that
 * match the given string; if pcszMatch is NULL, all direct child
 * elements are returned.
 * @param children out: list of nodes to which children will be appended.
 * @param pcszMatch in: match string, or NULL to return all children.
 * @return Number of items appended to the list (0 if none).
 */
int Node::getChildElements(NodesList &children,
                              const char *pcszMatch /*= NULL*/)
    const
{
    int i = 0;
    Data::InternalNodesList::const_iterator
        it,
        last = m->children.end();
    for (it = m->children.begin();
         it != last;
         ++it)
    {
        // export this child node if ...
        if (    (!pcszMatch)    // the caller wants all nodes or
             || (!strcmp(pcszMatch, (**it).getName())) // the element name matches
           )
        {
            children.push_back((*it).get());
            ++i;
        }
    }
    return i;
}

/**
 * Returns the first child element whose name matches pcszMatch.
 * @param pcszMatch
 * @return
 */
const Node* Node::findChildElement(const char *pcszMatch)
    const
{
    Data::InternalNodesList::const_iterator
        it,
        last = m->children.end();
    for (it = m->children.begin();
         it != last;
         ++it)
    {
        if (!strcmp(pcszMatch, (**it).getName())) // the element name matches
            return (*it).get();
    }

    return NULL;
}

/**
 * Returns the first child element whose "id" attribute matches pcszId.
 * @param pcszId identifier to look for.
 * @return child element or NULL if not found.
 */
const Node* Node::findChildElementFromId(const char *pcszId) const
{
    Data::InternalNodesList::const_iterator
        it,
        last = m->children.end();
    for (it = m->children.begin();
         it != last;
         ++it)
    {
        const Node *pElem = (*it).get();
        const Node *pAttr;
        if (    ((pAttr = pElem->findAttribute("id")))
             && (!strcmp(pAttr->getValue(), pcszId))
           )
            return pElem;
    }

    return NULL;
}

/**
 *
 * @param pcszMatch
 * @return
 */
const Node* Node::findAttribute(const char *pcszMatch) const
{
    Data::AttributesMap::const_iterator it;

    it = m->attribs.find(pcszMatch);
    if (it != m->attribs.end())
        return it->second.get();

    return NULL;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a string.
 *
 * @param pcszMatch name of attribute to find.
 * @param str out: attribute value
 * @return TRUE if attribute was found and str was thus updated.
 */
bool Node::getAttributeValue(const char *pcszMatch, com::Utf8Str &str) const
{
    const Node* pAttr;
    if ((pAttr = findAttribute(pcszMatch)))
    {
        str = pAttr->getValue();
        return true;
    }

    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as a signed long integer. This calls
 * RTStrToInt64Ex internally and will only output the integer if that
 * function returns no error.
 *
 * @param pcszMatch name of attribute to find.
 * @param i out: attribute value
 * @return TRUE if attribute was found and str was thus updated.
 */
bool Node::getAttributeValue(const char *pcszMatch, int64_t &i) const
{
    com::Utf8Str str;
    if (    (getAttributeValue(pcszMatch, str))
         && (VINF_SUCCESS == RTStrToInt64Ex(str.c_str(), NULL, 10, &i))
       )
        return true;

    return false;
}

/**
 * Convenience method which attempts to find the attribute with the given
 * name and returns its value as an unsigned long integer.This calls
 * RTStrToUInt64Ex internally and will only output the integer if that
 * function returns no error.
 *
 * @param pcszMatch name of attribute to find.
 * @param i out: attribute value
 * @return TRUE if attribute was found and str was thus updated.
 */
bool Node::getAttributeValue(const char *pcszMatch, uint64_t &i) const
{
    com::Utf8Str str;
    if (    (getAttributeValue(pcszMatch, str))
         && (VINF_SUCCESS == RTStrToUInt64Ex(str.c_str(), NULL, 10, &i))
       )
        return true;

    return false;
}

/*
 * NodesLoop
 *
 */

struct NodesLoop::Data
{
    NodesList listElements;
    NodesList::const_iterator it;
};

NodesLoop::NodesLoop(const Node &node, const char *pcszMatch /* = NULL */)
{
    m = new Data;
    node.getChildElements(m->listElements, pcszMatch);
    m->it = m->listElements.begin();
}

NodesLoop::~NodesLoop()
{
    delete m;
}


/**
 * Handy convenience helper for looping over all child elements. Create an
 * instance of NodesLoop on the stack and call this method until it returns
 * NULL, like this:
 * <code>
 *      xml::Node node;         // should point to an element
 *      xml::NodesLoop loop(node, "child");  // find all "child" elements under node
 *      const xml::Node *pChild = NULL;
 *      while (pChild = loop.forAllNodes())
 *          ...;
 * </code>
 * @param node
 * @param pcszMatch
 * @return
 */
const Node* NodesLoop::forAllNodes() const
{
    const Node *pNode = NULL;

    if (m->it != m->listElements.end())
    {
        pNode = *(m->it);
        ++(m->it);
    }

    return pNode;
}

////////////////////////////////////////////////////////////////////////////////
//
// Document class
//
////////////////////////////////////////////////////////////////////////////////

struct Document::Data
{
    xmlDocPtr   plibDocument;
    Node        *pRootElement;

    Data()
    {
        plibDocument = NULL;
        pRootElement = NULL;
    }

    ~Data()
    {
        reset();
    }

    void reset()
    {
        if (plibDocument)
        {
            xmlFreeDoc(plibDocument);
            plibDocument = NULL;
        }
        if (pRootElement)
        {
            delete pRootElement;
            pRootElement = NULL;
        }
    }

    void copyFrom(const Document::Data *p)
    {
        if (p->plibDocument)
        {
            plibDocument = xmlCopyDoc(p->plibDocument,
                                      1);      // recursive == copy all
        }
    }
};

Document::Document()
    : m(new Data)
{
}

Document::Document(const Document &x)
    : m(new Data)
{
    m->copyFrom(x.m);
};

Document& Document::operator=(const Document &x)
{
    m->reset();
    m->copyFrom(x.m);
    return *this;
};

Document::~Document()
{
    delete m;
}

/**
 * private method to refresh all internal structures after the internal pDocument
 * has changed. Called from XmlFileParser::read(). m->reset() must have been
 * called before to make sure all members except the internal pDocument are clean.
 */
void Document::refreshInternals() // private
{
    m->pRootElement = new Node();
    m->pRootElement->m->plibNode = xmlDocGetRootElement(m->plibDocument);
    m->pRootElement->m->pcszName = (const char*)m->pRootElement->m->plibNode->name;

    m->pRootElement->buildChildren();
}

/**
 * Returns the root element of the document, or NULL if the document is empty.
 * @return
 */
const Node* Document::getRootElement() const
{
    return m->pRootElement;
}

/**
 * Creates a new element node and sets it as the root element. This will
 * only work if the document is empty; otherwise EDocumentNotEmpty is thrown.
 */
Node* Document::createRootElement(const char *pcszRootElementName)
{
    if (m->pRootElement)
        throw EDocumentNotEmpty(RT_SRC_POS);

    m->plibDocument = xmlNewDoc((const xmlChar*)"1.0");
    if (!(m->pRootElement = new Node()))
        throw ENoMemory();
    Node::Data *pNodeData = m->pRootElement->m;
    if (!(pNodeData->plibNode = xmlNewNode(NULL,        // namespace
                                           (const xmlChar*)pcszRootElementName)))
        throw ENoMemory();
    pNodeData->pcszName = (const char*)pNodeData->plibNode->name;

    return m->pRootElement;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlParserBase class
//
////////////////////////////////////////////////////////////////////////////////

XmlParserBase::XmlParserBase()
{
    m_ctxt = xmlNewParserCtxt();
    if (m_ctxt == NULL)
        throw ENoMemory();
}

XmlParserBase::~XmlParserBase()
{
    xmlFreeParserCtxt (m_ctxt);
    m_ctxt = NULL;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlFileParser class
//
////////////////////////////////////////////////////////////////////////////////

struct XmlFileParser::Data
{
    xmlParserCtxtPtr ctxt;
    com::Utf8Str strXmlFilename;

    Data()
    {
        if (!(ctxt = xmlNewParserCtxt()))
            throw xml::ENoMemory();
    }

    ~Data()
    {
        xmlFreeParserCtxt(ctxt);
        ctxt = NULL;
    }
};

XmlFileParser::XmlFileParser()
    : XmlParserBase(),
      m(new Data())
{
}

XmlFileParser::~XmlFileParser()
{
}

struct ReadWriteContext
{
    File file;
    com::Utf8Str error;

    ReadWriteContext(const char *pcszFilename)
        : file(File::Mode_Read, pcszFilename)     // @todo must be write for writer
    {
    }

    void setError(const xml::Error &x)
    {
        error = x.what();
    }

    void setError(const std::exception &x)
    {
        error = x.what();
    }
};

/**
 * Reads the given file and fills the given Document object with its contents.
 * Throws XmlError on parsing errors.
 *
 * The document that is passed in will be reset before being filled if not empty.
 *
 * @param pcszFilename in: name fo file to parse.
 * @param doc out: document to be reset and filled with data according to file contents.
 */
void XmlFileParser::read(const char *pcszFilename,
                         Document &doc)
{
    GlobalLock lock();
//     global.setExternalEntityLoader(ExternalEntityLoader);

    m->strXmlFilename = pcszFilename;

    ReadWriteContext context(pcszFilename);
    doc.m->reset();
    if (!(doc.m->plibDocument = xmlCtxtReadIO(m->ctxt,
                                              ReadCallback,
                                              CloseCallback,
                                              &context,
                                              pcszFilename,
                                              NULL,       // encoding = auto
                                              XML_PARSE_NOBLANKS)))
        throw XmlError(xmlCtxtGetLastError(m->ctxt));

    doc.refreshInternals();
}

// static
int XmlFileParser::ReadCallback(void *aCtxt, char *aBuf, int aLen)
{
    ReadWriteContext *pContext = static_cast<ReadWriteContext*>(aCtxt);

    /* To prevent throwing exceptions while inside libxml2 code, we catch
     * them and forward to our level using a couple of variables. */

    try
    {
        return pContext->file.read(aBuf, aLen);
    }
    catch (const xml::EIPRTFailure &err) { pContext->setError(err); }
    catch (const xml::Error &err) { pContext->setError(err); }
    catch (const std::exception &err) { pContext->setError(err); }
    catch (...) { pContext->setError(xml::LogicError(RT_SRC_POS)); }

    return -1 /* failure */;
}

int XmlFileParser::CloseCallback(void *aCtxt)
{
    /// @todo to be written

    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// XmlFileWriter class
//
////////////////////////////////////////////////////////////////////////////////

struct XmlFileWriter::Data
{
    Document *pDoc;
};

XmlFileWriter::XmlFileWriter(Document &doc)
{
    m = new Data();
    m->pDoc = &doc;
}

XmlFileWriter::~XmlFileWriter()
{
    delete m;
}

int XmlFileWriter::WriteCallback(void *aCtxt, const char *aBuf, int aLen)
{
    ReadWriteContext *pContext = static_cast<ReadWriteContext*>(aCtxt);

    /* To prevent throwing exceptions while inside libxml2 code, we catch
     * them and forward to our level using a couple of variables. */
    try
    {
        return pContext->file.write(aBuf, aLen);
    }
    catch (const xml::EIPRTFailure &err) { pContext->setError(err); }
    catch (const xml::Error &err) { pContext->setError(err); }
    catch (const std::exception &err) { pContext->setError(err); }
    catch (...) { pContext->setError(xml::LogicError(RT_SRC_POS)); }

    return -1 /* failure */;
}

int XmlFileWriter::CloseCallback(void *aCtxt)
{
    /// @todo to be written

    return -1;
}

void XmlFileWriter::write(const char *pcszFilename)
{
    ReadWriteContext context(pcszFilename);

    GlobalLock lock();

    /* serialize to the stream */
    xmlIndentTreeOutput = 1;
    xmlTreeIndentString = "  ";
    xmlSaveNoEmptyTags = 0;

    xmlSaveCtxtPtr saveCtxt;
    if (!(saveCtxt = xmlSaveToIO(WriteCallback,
                                 CloseCallback,
                                 &context,
                                 NULL,
                                 XML_SAVE_FORMAT)))
        throw xml::LogicError(RT_SRC_POS);

    long rc = xmlSaveDoc(saveCtxt, m->pDoc->m->plibDocument);
    if (rc == -1)
    {
        /* look if there was a forwared exception from the lower level */
//         if (m->trappedErr.get() != NULL)
//             m->trappedErr->rethrow();

        /* there must be an exception from the Output implementation,
         * otherwise the save operation must always succeed. */
        throw xml::LogicError(RT_SRC_POS);
    }

    xmlSaveClose(saveCtxt);
}


} // end namespace xml


