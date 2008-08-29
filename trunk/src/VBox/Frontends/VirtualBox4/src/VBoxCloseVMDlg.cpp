/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxCloseVMDlg class implementation
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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

#include "VBoxCloseVMDlg.h"
#include "VBoxProblemReporter.h"

/* Qt includes */
#include <QPushButton>

VBoxCloseVMDlg::VBoxCloseVMDlg (QWidget *aParent)
    : QIWithRetranslateUI2<QIDialog> (aParent, Qt::Sheet)
{
    /* Apply UI decorations */
    Ui::VBoxCloseVMDlg::setupUi (this);

#ifdef Q_WS_MAC
    /* Make some more space around the content */
    hboxLayout->setContentsMargins (40, 0, 40, 0);
    vboxLayout2->insertSpacing (1, 20);
    /* and more space between the radio buttons */
    gridLayout->setSpacing (15);
#endif /* Q_WS_MAC */
    /* Set fixed size */
    setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed);

    connect (mButtonBox, SIGNAL (helpRequested()),
             &vboxProblem(), SLOT (showHelpHelpDialog()));
}

void VBoxCloseVMDlg::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxCloseVMDlg::retranslateUi (this);
}

