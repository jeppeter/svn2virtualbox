/** @file
 * VBox Qt GUI - UIAddDiskEncryptionPasswordDialog class declaration.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___UIAddDiskEncryptionPasswordDialog_h___
#define ___UIAddDiskEncryptionPasswordDialog_h___

/* Qt includes: */
#include <QDialog>
#include <QMultiMap>
#include <QMap>

/* GUI includes: */
#include "QIWithRetranslateUI.h"

/* Forward declarations: */
class UIEncryptionDataTable;
class QLabel;

/* Type definitions: */
typedef QMultiMap<QString, QString> EncryptedMediumMap;
typedef QMap<QString, QString> EncryptionPasswordMap;
typedef QMap<QString, bool> EncryptionPasswordStatusMap;

/** QDialog reimplementation used to
  * allow the user to enter disk encryption passwords for particular password ids. */
class UIAddDiskEncryptionPasswordDialog : public QIWithRetranslateUI<QDialog>
{
    Q_OBJECT;

public:

    /** Constructor.
      * @param pParent          being passed to the base-class,
      * @param strMachineName   holds the name of the machine we show this dialog for,
      * @param encryptedMediums contains the lists of medium ids (values) encrypted with passwords with ids (keys). */
    UIAddDiskEncryptionPasswordDialog(QWidget *pParent, const QString &strMachineName, const EncryptedMediumMap &encryptedMediums);

    /** Returns the shallow copy of the encryption password map
      * acquired from the UIEncryptionDataTable instance. */
    EncryptionPasswordMap encryptionPasswords() const;

private:

    /** Prepare routine. */
    void prepare();

    /** Translation routine. */
    void retranslateUi();

    /** Holds the name of the machine we show this dialog for. */
    const QString m_strMachineName;

    /** Holds the encrypted medium map reference. */
    const EncryptedMediumMap &m_encryptedMediums;

    /** Holds the description label instance. */
    QLabel *m_pLabelDescription;
    /** Holds the encryption-data table instance. */
    UIEncryptionDataTable *m_pTableEncryptionData;
};

#endif /* !___UIAddDiskEncryptionPasswordDialog_h___ */
