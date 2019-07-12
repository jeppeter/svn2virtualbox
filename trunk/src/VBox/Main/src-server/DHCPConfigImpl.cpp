/* $Id$ */
/** @file
 * VirtualBox Main - IDHCPConfig, IDHCPConfigGlobal, IDHCPConfigGroup, IDHCPConfigIndividual implementation.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_DHCPCONFIG
#include "DHCPConfigImpl.h"
#include "LoggingNew.h"

#include <iprt/errcore.h>
#include <iprt/net.h>
#include <iprt/cpp/utils.h>
#include <iprt/cpp/xml.h>

#include <VBox/com/array.h>
#include <VBox/settings.h>

#include "AutoCaller.h"
#include "MachineImpl.h"
#include "VirtualBoxImpl.h"



/*********************************************************************************************************************************
*   DHCPConfig Implementation                                                                                                    *
*********************************************************************************************************************************/

HRESULT DHCPConfig::i_initWithDefaults(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent)
{
    unconst(m_pVirtualBox) = a_pVirtualBox;
    unconst(m_pParent)     = a_pParent;
    return S_OK;
}


HRESULT DHCPConfig::i_initWithSettings(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent, const settings::DHCPConfig &rConfig)
{
    unconst(m_pVirtualBox) = a_pVirtualBox;
    unconst(m_pParent)     = a_pParent;

    m_secMinLeaseTime     = rConfig.secMinLeaseTime;
    m_secDefaultLeaseTime = rConfig.secDefaultLeaseTime;
    m_secMaxLeaseTime     = rConfig.secMaxLeaseTime;

    for (settings::DhcpOptionMap::const_iterator it = rConfig.OptionMap.begin(); it != rConfig.OptionMap.end(); ++it)
    {
        try
        {
            m_OptionMap[it->first] = settings::DhcpOptValue(it->second.strValue, it->second.enmEncoding);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}


HRESULT DHCPConfig::i_saveSettings(settings::DHCPConfig &a_rDst)
{
    /* lease times */
    a_rDst.secMinLeaseTime     = m_secMinLeaseTime;
    a_rDst.secDefaultLeaseTime = m_secDefaultLeaseTime;
    a_rDst.secMaxLeaseTime     = m_secMaxLeaseTime;

    /* Options: */
    try
    {
        a_rDst.OptionMap = m_OptionMap;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}


HRESULT DHCPConfig::i_getScope(DHCPConfigScope_T *aScope)
{
    /* No locking needed. */
    *aScope = m_enmScope;
    return S_OK;
}


HRESULT DHCPConfig::i_getMinLeaseTime(ULONG *aMinLeaseTime)
{
    AutoReadLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
    *aMinLeaseTime = m_secMinLeaseTime;
    return S_OK;
}


HRESULT DHCPConfig::i_setMinLeaseTime(ULONG aMinLeaseTime)
{
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        m_secMinLeaseTime = aMinLeaseTime;
    }
    return i_doWriteConfig();
}


HRESULT DHCPConfig::i_getDefaultLeaseTime(ULONG *aDefaultLeaseTime)
{
    AutoReadLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
    *aDefaultLeaseTime = m_secDefaultLeaseTime;
    return S_OK;
}


HRESULT DHCPConfig::i_setDefaultLeaseTime(ULONG aDefaultLeaseTime)
{
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        m_secDefaultLeaseTime = aDefaultLeaseTime;
    }
    return i_doWriteConfig();
}


HRESULT DHCPConfig::i_getMaxLeaseTime(ULONG *aMaxLeaseTime)
{
    AutoReadLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
    *aMaxLeaseTime = m_secMaxLeaseTime;
    return S_OK;
}


HRESULT DHCPConfig::i_setMaxLeaseTime(ULONG aMaxLeaseTime)
{
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        m_secMaxLeaseTime = aMaxLeaseTime;
    }
    return i_doWriteConfig();
}


HRESULT DHCPConfig::i_setOption(DhcpOpt_T aOption, DHCPOptionEncoding_T aEncoding, const com::Utf8Str &aValue)
{
    /** @todo validate option value format. */
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        try
        {
            m_OptionMap[aOption] = settings::DhcpOptValue(aValue, aEncoding);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }
    i_doWriteConfig();
    return S_OK;
}


HRESULT DHCPConfig::i_removeOption(DhcpOpt_T aOption)
{
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        settings::DhcpOptionMap::iterator it = m_OptionMap.find(aOption);
        if (it != m_OptionMap.end())
            m_OptionMap.erase(it);
        else
            return m_pHack->setError(VBOX_E_OBJECT_NOT_FOUND, m_pHack->tr("DHCP option %u was not found"), aOption);
    }
    return i_doWriteConfig();
}


HRESULT DHCPConfig::i_removeAllOptions()
{
    {
        AutoWriteLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
        m_OptionMap.erase(m_OptionMap.begin(), m_OptionMap.end());
    }
    return i_doWriteConfig();
}


HRESULT DHCPConfig::i_getOption(DhcpOpt_T aOption, DHCPOptionEncoding_T *aEncoding, com::Utf8Str &aValue)
{
    AutoReadLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
    settings::DhcpOptionMap::const_iterator it = m_OptionMap.find(aOption);
    if (it != m_OptionMap.end())
    {
        *aEncoding = it->second.enmEncoding;
        return aValue.assignEx(it->second.strValue);
    }
    return m_pHack->setError(VBOX_E_OBJECT_NOT_FOUND, m_pHack->tr("DHCP option %u was not found"), aOption);
}


HRESULT DHCPConfig::i_getAllOptions(std::vector<DhcpOpt_T> &aOptions, std::vector<DHCPOptionEncoding_T> &aEncodings,
                                    std::vector<com::Utf8Str> &aValues)
{
    AutoReadLock alock(m_pHack COMMA_LOCKVAL_SRC_POS);
    try
    {
        aOptions.resize(m_OptionMap.size());
        aEncodings.resize(m_OptionMap.size());
        aValues.resize(m_OptionMap.size());
        size_t i = 0;
        for (settings::DhcpOptionMap::iterator it = m_OptionMap.begin(); it != m_OptionMap.end(); ++it)
        {
            aOptions[i]   = it->first;
            aEncodings[i] = it->second.enmEncoding;
            aValues[i]    = it->second.strValue;
        }
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}


/**
 * Causes the global VirtualBox configuration file to be written
 *
 * @returns COM status code.
 *
 * @note    Must hold no locks when this is called!
 */
HRESULT DHCPConfig::i_doWriteConfig()
{
    AssertPtrReturn(m_pVirtualBox, E_FAIL);

    AutoWriteLock alock(m_pVirtualBox COMMA_LOCKVAL_SRC_POS);
    return m_pVirtualBox->i_saveSettings();
}


/**
 * Produces the Dhcpd configuration.
 *
 * The base class only saves DHCP options.
 *
 * @param   pElmConfig  The element where to put the configuration.
 * @throws  std::bad_alloc
 */
void DHCPConfig::i_writeDhcpdConfig(xml::ElementNode *pElmConfig)
{
    if (m_secMinLeaseTime > 0 )
        pElmConfig->setAttribute("secMinLeaseTime", (uint32_t)m_secMinLeaseTime);
    if (m_secDefaultLeaseTime > 0 )
        pElmConfig->setAttribute("secDefaultLeaseTime", (uint32_t)m_secDefaultLeaseTime);
    if (m_secMaxLeaseTime > 0 )
        pElmConfig->setAttribute("secMaxLeaseTime", (uint32_t)m_secMaxLeaseTime);

    for (settings::DhcpOptionMap::const_iterator it = m_OptionMap.begin(); it != m_OptionMap.end(); ++it)
    {
        xml::ElementNode *pOption = pElmConfig->createChild("Option");
        pOption->setAttribute("name", (int)it->first);
        pOption->setAttribute("encoding", it->second.enmEncoding);
        pOption->setAttribute("value", it->second.strValue);
    }
}



/*********************************************************************************************************************************
*   DHCPGlobalConfig Implementation                                                                                              *
*********************************************************************************************************************************/
#undef  LOG_GROUP
#define LOG_GROUP LOG_GROUP_MAIN_DHCPGLOBALCONFIG

HRESULT DHCPGlobalConfig::initWithDefaults(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithDefaults(a_pVirtualBox, a_pParent);
    if (SUCCEEDED(hrc))
        hrc = i_setOption(DhcpOpt_SubnetMask, DHCPOptionEncoding_Legacy, "0.0.0.0");

    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    return hrc;
}


HRESULT DHCPGlobalConfig::initWithSettings(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent, const settings::DHCPConfig &rConfig)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithSettings(a_pVirtualBox, a_pParent, rConfig);
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);
    return hrc;
}


void DHCPGlobalConfig::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone())
        autoUninitSpan.setSucceeded();
}


HRESULT DHCPGlobalConfig::i_saveSettings(settings::DHCPConfig &a_rDst)
{
    return DHCPConfig::i_saveSettings(a_rDst);
}


/**
 * For getting the network mask option value (IDHCPServer::netmask attrib).
 *
 * @returns COM status code.
 * @param   a_rDst          Where to return it.
 * @throws  nothing
 */
HRESULT DHCPGlobalConfig::i_getNetworkMask(com::Utf8Str &a_rDst)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    settings::DhcpOptionMap::const_iterator it = m_OptionMap.find(DhcpOpt_SubnetMask);
    if (it != m_OptionMap.end())
    {
        if (it->second.enmEncoding == DHCPOptionEncoding_Legacy)
            return a_rDst.assignEx(it->second.strValue);
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("DHCP option DhcpOpt_SubnetMask is not in a legacy encoding"));
    }
    return setError(VBOX_E_OBJECT_NOT_FOUND, tr("DHCP option DhcpOpt_SubnetMask was not found"));
}


/**
 * For setting the network mask option value (IDHCPServer::netmask attrib).
 *
 * @returns COM status code.
 * @param   a_rSrc          The new value.
 * @throws  nothing
 */
HRESULT DHCPGlobalConfig::i_setNetworkMask(const com::Utf8Str &a_rSrc)
{
    /* Validate it before setting it: */
    RTNETADDRIPV4 AddrIgnored;
    int vrc = RTNetStrToIPv4Addr(a_rSrc.c_str(), &AddrIgnored);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid IPv4 netmask '%s': %Rrc"), a_rSrc.c_str(), vrc);

    return i_setOption(DhcpOpt_SubnetMask, DHCPOptionEncoding_Legacy, a_rSrc);
}


/**
 * Overriden to ensure the sanity of the DhcpOpt_SubnetMask option.
 */
HRESULT DHCPGlobalConfig::i_setOption(DhcpOpt_T aOption, DHCPOptionEncoding_T aEncoding, const com::Utf8Str &aValue)
{
    if (aOption != DhcpOpt_SubnetMask || aEncoding == DHCPOptionEncoding_Legacy)
        return DHCPConfig::i_setOption(aOption, aEncoding, aValue);
    return setError(E_FAIL, tr("DhcpOpt_SubnetMask must use DHCPOptionEncoding_Legacy as it is reflected by IDHCPServer::networkMask"));
}


/**
 * Overriden to ensure the sanity of the DhcpOpt_SubnetMask option.
 */
HRESULT DHCPGlobalConfig::i_removeOption(DhcpOpt_T aOption)
{
    if (aOption != DhcpOpt_SubnetMask)
        return DHCPConfig::i_removeOption(aOption);
    return setError(E_FAIL, tr("DhcpOpt_SubnetMask cannot be removed as it reflects IDHCPServer::networkMask"));
}


/**
 * Overriden to preserve the DhcpOpt_SubnetMask option.
 */
HRESULT DHCPGlobalConfig::i_removeAllOptions()
{
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        settings::DhcpOptionMap::iterator it = m_OptionMap.find(DhcpOpt_SubnetMask);
        m_OptionMap.erase(m_OptionMap.begin(), it);
        if (it != m_OptionMap.end())
        {
            ++it;
            if (it != m_OptionMap.end())
                m_OptionMap.erase(it, m_OptionMap.end());
        }
    }

    return i_doWriteConfig();
}



/*********************************************************************************************************************************
*   DHCPIndividualConfig Implementation                                                                                          *
*********************************************************************************************************************************/
#undef  LOG_GROUP
#define LOG_GROUP LOG_GROUP_MAIN_DHCPINDIVIDUALCONFIG

HRESULT DHCPIndividualConfig::initWithMachineIdAndSlot(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent,
                                                       com::Guid const &a_idMachine, ULONG a_uSlot, uint32_t a_uMACAddressVersion)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithDefaults(a_pVirtualBox, a_pParent);
    if (SUCCEEDED(hrc))
    {
        unconst(m_enmScope)          = DHCPConfigScope_MachineNIC;
        unconst(m_idMachine)         = a_idMachine;
        unconst(m_uSlot)             = a_uSlot;
        m_uMACAddressResolvedVersion = a_uMACAddressVersion;

        autoInitSpan.setSucceeded();
    }
    return hrc;
}


HRESULT DHCPIndividualConfig::initWithMACAddress(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent, PCRTMAC a_pMACAddress)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithDefaults(a_pVirtualBox, a_pParent);
    if (SUCCEEDED(hrc))
    {
        unconst(m_enmScope)   = DHCPConfigScope_MAC;
        unconst(m_MACAddress) = *a_pMACAddress;

        autoInitSpan.setSucceeded();
    }
    return hrc;
}


HRESULT DHCPIndividualConfig::initWithSettingsAndMachineIdAndSlot(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent,
                                                                  settings::DHCPIndividualConfig const &rConfig,
                                                                  com::Guid const &a_idMachine, ULONG a_uSlot,
                                                                  uint32_t a_uMACAddressVersion)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithSettings(a_pVirtualBox, a_pParent, rConfig);
    if (SUCCEEDED(hrc))
    {
        unconst(m_enmScope)          = DHCPConfigScope_MachineNIC;
        unconst(m_idMachine)         = a_idMachine;
        unconst(m_uSlot)             = a_uSlot;
        m_uMACAddressResolvedVersion = a_uMACAddressVersion;
        m_strFixedAddress            = rConfig.strFixedAddress;

        autoInitSpan.setSucceeded();
    }
    return hrc;
}


HRESULT DHCPIndividualConfig::initWithSettingsAndMACAddress(VirtualBox *a_pVirtualBox, DHCPServer *a_pParent,
                                                            settings::DHCPIndividualConfig const &rConfig, PCRTMAC a_pMACAddress)
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT hrc = DHCPConfig::i_initWithSettings(a_pVirtualBox, a_pParent, rConfig);
    if (SUCCEEDED(hrc))
    {
        unconst(m_enmScope)   = DHCPConfigScope_MAC;
        unconst(m_MACAddress) = *a_pMACAddress;
        m_strFixedAddress     = rConfig.strFixedAddress;

        autoInitSpan.setSucceeded();
    }
    return hrc;
}


void    DHCPIndividualConfig::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone())
        autoUninitSpan.setSucceeded();
}


HRESULT DHCPIndividualConfig::i_saveSettings(settings::DHCPIndividualConfig &a_rDst)
{
    a_rDst.uSlot = m_uSlot;
    int vrc = a_rDst.strMACAddress.printfNoThrow("%RTmac", &m_MACAddress);
    if (m_idMachine.isValid() && !m_idMachine.isZero() && RT_SUCCESS(vrc))
        vrc = a_rDst.strVMName.printfNoThrow("%RTuuid", m_idMachine.raw());
    if (RT_SUCCESS(vrc))
        vrc = a_rDst.strFixedAddress.assignNoThrow(m_strFixedAddress);
    if (RT_SUCCESS(vrc))
        return DHCPConfig::i_saveSettings(a_rDst);
    return E_OUTOFMEMORY;;
}


HRESULT DHCPIndividualConfig::getMACAddress(com::Utf8Str &aMACAddress)
{
    /* No locking needed here (the MAC address, machine UUID and NIC slot number cannot change). */
    RTMAC MACAddress;
    if (m_enmScope == DHCPConfigScope_MAC)
        MACAddress = m_MACAddress;
    else
    {
        HRESULT hrc = i_getMachineMAC(&MACAddress);
        if (FAILED(hrc))
            return hrc;
    }

    /* Format the return string: */
    int vrc = aMACAddress.printfNoThrow("%RTmac", &m_MACAddress);
    return RT_SUCCESS(vrc) ? S_OK : E_OUTOFMEMORY;
}


HRESULT DHCPIndividualConfig::getMachineId(com::Guid &aId)
{
    AutoReadLock(this COMMA_LOCKVAL_SRC_POS);
    aId = m_idMachine;
    return S_OK;
}


HRESULT DHCPIndividualConfig::getSlot(ULONG *aSlot)
{
    AutoReadLock(this COMMA_LOCKVAL_SRC_POS);
    *aSlot = m_uSlot;
    return S_OK;
}

HRESULT DHCPIndividualConfig::getFixedAddress(com::Utf8Str &aFixedAddress)
{
    AutoReadLock(this COMMA_LOCKVAL_SRC_POS);
    return aFixedAddress.assignEx(m_strFixedAddress);
}


HRESULT DHCPIndividualConfig::setFixedAddress(const com::Utf8Str &aFixedAddress)
{
    if (aFixedAddress.isNotEmpty())
    {
        RTNETADDRIPV4 AddrIgnored;
        int vrc = RTNetStrToIPv4Addr(aFixedAddress.c_str(), &AddrIgnored);
        if (RT_FAILURE(vrc))
            return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid IPv4 address '%s': %Rrc"), aFixedAddress.c_str(), vrc);
    }

    {
        AutoWriteLock(this COMMA_LOCKVAL_SRC_POS);
        m_strFixedAddress = aFixedAddress;
    }
    return i_doWriteConfig();
}


/**
 * Gets the MAC address of m_idMachine + m_uSlot.
 *
 * @returns COM status code w/ setError.
 * @param   pMACAddress     Where to return the address.
 *
 * @note    Must be called without holding any DHCP related locks as that would
 *          be lock order violation.  The m_idMachine and m_uSlot values are
 *          practically const, so we don't need any locks here anyway.
 */
HRESULT DHCPIndividualConfig::i_getMachineMAC(PRTMAC pMACAddress)
{
    ComObjPtr<Machine> ptrMachine;
    HRESULT hrc = m_pVirtualBox->i_findMachine(m_idMachine, false /*fPermitInaccessible*/, true /*aSetError*/, &ptrMachine);
    if (SUCCEEDED(hrc))
    {
        ComPtr<INetworkAdapter> ptrNetworkAdapter;
        hrc = ptrMachine->GetNetworkAdapter(m_uSlot, ptrNetworkAdapter.asOutParam());
        if (SUCCEEDED(hrc))
        {
            com::Bstr bstrMACAddress;
            hrc = ptrNetworkAdapter->COMGETTER(MACAddress)(bstrMACAddress.asOutParam());
            if (SUCCEEDED(hrc))
            {
                Utf8Str strMACAddress;
                try
                {
                    strMACAddress = bstrMACAddress;
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }

                int vrc = RTNetStrToMacAddr(strMACAddress.c_str(), pMACAddress);
                if (RT_SUCCESS(vrc))
                    hrc = S_OK;
                else
                    hrc = setError(hrc, tr("INetworkAdapter returned bogus MAC address '%ls'"), bstrMACAddress.raw());
            }
        }
    }
    return hrc;
}


HRESULT DHCPIndividualConfig::i_resolveMACAddress(uint32_t uVersion)
{
    HRESULT hrc;
    if (m_enmScope == DHCPConfigScope_MachineNIC)
    {
        RTMAC MACAddress;
        hrc = i_getMachineMAC(&MACAddress);
        if (SUCCEEDED(hrc))
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if ((int32_t)(m_uMACAddressResolvedVersion - uVersion) >= 0)
            {
                m_uMACAddressResolvedVersion = uVersion;
                m_MACAddress                 = MACAddress;
            }
        }
    }
    else
        hrc = S_OK;
    return hrc;
}


/**
 * Overridden to write out additional config.
 */
void DHCPIndividualConfig::i_writeDhcpdConfig(xml::ElementNode *pElmConfig)
{
    char szTmp[RTUUID_STR_LENGTH + 32];
    RTStrPrintf(szTmp, sizeof(szTmp), "%RTmac", &m_MACAddress);
    pElmConfig->setAttribute("MACAddress", szTmp);

    if (m_enmScope == DHCPConfigScope_MachineNIC)
    {
        RTStrPrintf(szTmp, sizeof(szTmp), "%RTuuid/%u", m_idMachine.raw(), m_uSlot);
        pElmConfig->setAttribute("name", szTmp);
    }

    pElmConfig->setAttribute("fixedAddress", m_strFixedAddress);

    DHCPConfig::i_writeDhcpdConfig(pElmConfig);
}
