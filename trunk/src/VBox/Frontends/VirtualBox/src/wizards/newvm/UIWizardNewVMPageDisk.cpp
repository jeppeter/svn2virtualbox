/* $Id$ */
/** @file
 * VBox Qt GUI - UIWizardNewVMPageDisk class implementation.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMetaType>
#include <QRadioButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIMediaComboBox.h"
#include "UIMedium.h"
#include "UIMediumSelector.h"
#include "UIMediumSizeEditor.h"
#include "UIMessageCenter.h"
#include "UIWizardNewVD.h"
#include "UIWizardNewVMPageDisk.h"

/* COM includes: */
#include "CGuestOSType.h"
#include "CSystemProperties.h"

UIWizardNewVMPageDiskBase::UIWizardNewVMPageDiskBase()
    : m_fRecommendedNoDisk(false)
    , m_pDiskEmpty(0)
    , m_pDiskNew(0)
    , m_pDiskExisting(0)
    , m_pDiskSelector(0)
    , m_pDiskSelectionButton(0)
    , m_enmSelectedDiskSource(SelectedDiskSource_New)
{
}

SelectedDiskSource UIWizardNewVMPageDiskBase::selectedDiskSource() const
{
    return m_enmSelectedDiskSource;
}

void UIWizardNewVMPageDiskBase::setSelectedDiskSource(SelectedDiskSource enmSelectedDiskSource)
{
    m_enmSelectedDiskSource = enmSelectedDiskSource;
}

void UIWizardNewVMPageDiskBase::getWithFileOpenDialog()
{
    QUuid uMediumId;
    int returnCode = uiCommon().openMediumSelectorDialog(thisImp(), UIMediumDeviceType_HardDisk,
                                                           uMediumId,
                                                           fieldImp("machineFolder").toString(),
                                                           fieldImp("machineBaseName").toString(),
                                                           fieldImp("type").value<CGuestOSType>().GetId(),
                                                           false /* don't show/enable the create action: */);
    if (returnCode == static_cast<int>(UIMediumSelector::ReturnCode_Accepted) && !uMediumId.isNull())
    {
        m_pDiskSelector->setCurrentItem(uMediumId);
        m_pDiskSelector->setFocus();
    }
}

void UIWizardNewVMPageDiskBase::retranslateWidgets()
{
    if (m_pDiskEmpty)
        m_pDiskEmpty->setText(UIWizardNewVM::tr("&Do Not Add a Virtual Hard Disk"));
    if (m_pDiskNew)
        m_pDiskNew->setText(UIWizardNewVM::tr("&Create a Virtual Hard Disk Now"));
    if (m_pDiskExisting)
        m_pDiskExisting->setText(UIWizardNewVM::tr("U&se an Existing Virtual Hard Disk File"));
    if (m_pDiskSelectionButton)
        m_pDiskSelectionButton->setToolTip(UIWizardNewVM::tr("Choose a Virtual Hard Fisk File..."));
}

void UIWizardNewVMPageDiskBase::setEnableDiskSelectionWidgets(bool fEnabled)
{
    if (!m_pDiskSelector || !m_pDiskSelectionButton)
        return;

    m_pDiskSelector->setEnabled(fEnabled);
    m_pDiskSelectionButton->setEnabled(fEnabled);
}

QWidget *UIWizardNewVMPageDiskBase::createNewDiskWidgets()
{
    return new QWidget();
}

QWidget *UIWizardNewVMPageDiskBase::createDiskWidgets()
{
    QWidget *pDiskContainer = new QWidget;
    QGridLayout *pDiskLayout = new QGridLayout(pDiskContainer);
    pDiskLayout->setContentsMargins(0, 0, 0, 0);
    m_pDiskSourceButtonGroup = new QButtonGroup;
    m_pDiskEmpty = new QRadioButton;
    m_pDiskNew = new QRadioButton;
    m_pDiskExisting = new QRadioButton;
    m_pDiskSourceButtonGroup->addButton(m_pDiskEmpty);
    m_pDiskSourceButtonGroup->addButton(m_pDiskNew);
    m_pDiskSourceButtonGroup->addButton(m_pDiskExisting);
    QStyleOptionButton options;
    options.initFrom(m_pDiskExisting);
    int iWidth = m_pDiskExisting->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, &options, m_pDiskExisting);
    pDiskLayout->setColumnMinimumWidth(0, iWidth);
    m_pDiskSelector = new UIMediaComboBox;
    {
        m_pDiskSelector->setType(UIMediumDeviceType_HardDisk);
        m_pDiskSelector->repopulate();
    }
    m_pDiskSelectionButton = new QIToolButton;
    {
        m_pDiskSelectionButton->setAutoRaise(true);
        m_pDiskSelectionButton->setIcon(UIIconPool::iconSet(":/select_file_16px.png", ":/select_file_disabled_16px.png"));
    }
    pDiskLayout->addWidget(m_pDiskNew, 0, 0, 1, 6);
    pDiskLayout->addWidget(createNewDiskWidgets(), 1, 2, 3, 4);
    pDiskLayout->addWidget(m_pDiskExisting, 4, 0, 1, 6);
    pDiskLayout->addWidget(m_pDiskSelector, 5, 2, 1, 3);
    pDiskLayout->addWidget(m_pDiskSelectionButton, 5, 5, 1, 1);
    pDiskLayout->addWidget(m_pDiskEmpty, 6, 0, 1, 6);
    return pDiskContainer;
}

UIWizardNewVMPageDisk::UIWizardNewVMPageDisk()
    : m_pLabel(0)
    , m_fUserSetSize(false)

{
    prepare();
    qRegisterMetaType<CMedium>();
    qRegisterMetaType<SelectedDiskSource>();
    registerField("selectedDiskSource", this, "selectedDiskSource");

    registerField("mediumFormat", this, "mediumFormat");
    registerField("mediumVariant" /* KMediumVariant */, this, "mediumVariant");
    registerField("mediumPath", this, "mediumPath");
    registerField("mediumSize", this, "mediumSize");

    /* We do not have any UI elements for HDD format selection since we default to VDI in case of guided wizard mode: */
    bool fFoundVDI = false;
    CSystemProperties properties = uiCommon().virtualBox().GetSystemProperties();
    const QVector<CMediumFormat> &formats = properties.GetMediumFormats();
    foreach (const CMediumFormat &format, formats)
    {
        if (format.GetName() == "VDI")
        {
            m_mediumFormat = format;
            fFoundVDI = true;
        }
    }
    if (!fFoundVDI)
        AssertMsgFailed(("No medium format corresponding to VDI could be found!"));

    m_strDefaultExtension =  defaultExtension(m_mediumFormat);

    /* Since the medium format is static we can decide widget visibility here: */
    setWidgetVisibility(m_mediumFormat);
}

CMediumFormat UIWizardNewVMPageDisk::mediumFormat() const
{
    return m_mediumFormat;
}

QString UIWizardNewVMPageDisk::mediumPath() const
{
    return absoluteFilePath(toFileName(m_strDefaultName, m_strDefaultExtension), m_strDefaultPath);
}

void UIWizardNewVMPageDisk::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);

    m_pLabel = new QIRichTextLabel(this);
    pMainLayout->addWidget(m_pLabel);
    pMainLayout->addWidget(createDiskWidgets());

    pMainLayout->addStretch();
    setEnableDiskSelectionWidgets(m_enmSelectedDiskSource == SelectedDiskSource_Existing);
    setEnableNewDiskWidgets(m_enmSelectedDiskSource == SelectedDiskSource_New);

    createConnections();
}

QWidget *UIWizardNewVMPageDisk::createNewDiskWidgets()
{
    QWidget *pWidget = new QWidget;
    if (pWidget)
    {
        QVBoxLayout *pLayout = new QVBoxLayout(pWidget);
        if (pLayout)
        {
            pLayout->setContentsMargins(0, 0, 0, 0);

            /* Prepare size layout: */
            QGridLayout *pSizeLayout = new QGridLayout;
            if (pSizeLayout)
            {
                pSizeLayout->setContentsMargins(0, 0, 0, 0);

                /* Prepare Hard disk size label: */
                m_pMediumSizeEditorLabel = new QLabel(pWidget);
                if (m_pMediumSizeEditorLabel)
                {
                    m_pMediumSizeEditorLabel->setAlignment(Qt::AlignRight);
                    m_pMediumSizeEditorLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                    pSizeLayout->addWidget(m_pMediumSizeEditorLabel, 0, 0, Qt::AlignBottom);
                }
                /* Prepare Hard disk size editor: */
                m_pMediumSizeEditor = new UIMediumSizeEditor(pWidget);
                if (m_pMediumSizeEditor)
                {
                    m_pMediumSizeEditorLabel->setBuddy(m_pMediumSizeEditor);
                    pSizeLayout->addWidget(m_pMediumSizeEditor, 0, 1, 2, 1);
                }

                pLayout->addLayout(pSizeLayout);
            }

            /* Hard disk variant (dynamic vs. fixed) widgets: */
            pLayout->addWidget(createMediumVariantWidgets(false /* bool fWithLabels */));
        }
    }

    return pWidget;
}

void UIWizardNewVMPageDisk::createConnections()
{
    if (m_pDiskSourceButtonGroup)
        connect(m_pDiskSourceButtonGroup, static_cast<void(QButtonGroup::*)(QAbstractButton *)>(&QButtonGroup::buttonClicked),
                this, &UIWizardNewVMPageDisk::sltSelectedDiskSourceChanged);
    if (m_pDiskSelector)
        connect(m_pDiskSelector, static_cast<void(UIMediaComboBox::*)(int)>(&UIMediaComboBox::currentIndexChanged),
                this, &UIWizardNewVMPageDisk::sltMediaComboBoxIndexChanged);
    if (m_pDiskSelectionButton)
        connect(m_pDiskSelectionButton, &QIToolButton::clicked,
                this, &UIWizardNewVMPageDisk::sltGetWithFileOpenDialog);
    if (m_pMediumSizeEditor)
    {
        connect(m_pMediumSizeEditor, &UIMediumSizeEditor::sigSizeChanged,
                this, &UIWizardNewVMPageDisk::completeChanged);
        connect(m_pMediumSizeEditor, &UIMediumSizeEditor::sigSizeChanged,
                this, &UIWizardNewVMPageDisk::sltHandleSizeEditorChange);
    }
}

void UIWizardNewVMPageDisk::sltSelectedDiskSourceChanged()
{
    if (!m_pDiskSourceButtonGroup)
        return;

    if (m_pDiskSourceButtonGroup->checkedButton() == m_pDiskEmpty)
        setSelectedDiskSource(SelectedDiskSource_Empty);
    else if (m_pDiskSourceButtonGroup->checkedButton() == m_pDiskExisting)
    {
        setSelectedDiskSource(SelectedDiskSource_Existing);
        setVirtualDiskFromDiskCombo();
    }
    else
        setSelectedDiskSource(SelectedDiskSource_New);

    setEnableDiskSelectionWidgets(m_enmSelectedDiskSource == SelectedDiskSource_Existing);
    setEnableNewDiskWidgets(m_enmSelectedDiskSource == SelectedDiskSource_New);

    completeChanged();
}

void UIWizardNewVMPageDisk::sltMediaComboBoxIndexChanged()
{
    /* Make sure to set m_virtualDisk: */
    setVirtualDiskFromDiskCombo();
    emit completeChanged();
}

void UIWizardNewVMPageDisk::sltGetWithFileOpenDialog()
{
    getWithFileOpenDialog();
}

void UIWizardNewVMPageDisk::retranslateUi()
{
    setTitle(UIWizardNewVM::tr("Virtual Hard disk"));

    QString strRecommendedHDD = field("type").value<CGuestOSType>().isNull() ? QString() :
                                UICommon::formatSize(field("type").value<CGuestOSType>().GetRecommendedHDD());
    if (m_pLabel)
        m_pLabel->setText(UIWizardNewVM::tr("<p>If you wish you can add a virtual hard disk to the new machine. "
                                            "You can either create a new hard disk file or select an existing one. "
                                            "Alternatively you can create a virtual machine without a virtual hard disk.</p>"));

    UIWizardNewVMPageDiskBase::retranslateWidgets();
    UIWizardNewVDPageBaseFileType::retranslateWidgets();
    UIWizardNewVDPageBaseVariant::retranslateWidgets();
    UIWizardNewVDPageBaseSizeLocation::retranslateWidgets();
}

void UIWizardNewVMPageDisk::initializePage()
{
    retranslateUi();

    if (!field("type").canConvert<CGuestOSType>())
        return;

    CGuestOSType type = field("type").value<CGuestOSType>();

    if (type.GetRecommendedHDD() != 0)
    {
        if (m_pDiskNew)
        {
            m_pDiskNew->setFocus();
            m_pDiskNew->setChecked(true);
        }
        m_fRecommendedNoDisk = false;
    }
    else
    {
        if (m_pDiskEmpty)
        {
            m_pDiskEmpty->setFocus();
            m_pDiskEmpty->setChecked(true);
        }
        m_fRecommendedNoDisk = true;
     }
    if (m_pDiskSelector)
        m_pDiskSelector->setCurrentIndex(0);

    /* We set the medium name and path according to machine name/path and do let user change these in the guided mode: */
    QString strDefaultName = fieldImp("machineBaseName").toString();
    m_strDefaultName = strDefaultName.isEmpty() ? QString("NewVirtualDisk1") : strDefaultName;
    m_strDefaultPath = fieldImp("machineFolder").toString();
    /* Set the recommended disk size if user has already not done so: */
    if (m_pMediumSizeEditor && !m_fUserSetSize)
    {
        m_pMediumSizeEditor->blockSignals(true);
        setMediumSize(fieldImp("type").value<CGuestOSType>().GetRecommendedHDD());
        m_pMediumSizeEditor->blockSignals(false);
    }
}

void UIWizardNewVMPageDisk::cleanupPage()
{
    UIWizardPage::cleanupPage();
}

bool UIWizardNewVMPageDisk::isComplete() const
{
    if (selectedDiskSource() == SelectedDiskSource_New)
        return mediumSize() >= m_uMediumSizeMin && mediumSize() <= m_uMediumSizeMax;
    UIWizardNewVM *pWizard = wizardImp();
    AssertReturn(pWizard, false);
    if (selectedDiskSource() == SelectedDiskSource_Existing)
        return !pWizard->virtualDisk().isNull();

    return true;
}

bool UIWizardNewVMPageDisk::validatePage()
{
    bool fResult = true;

    /* Make sure user really intents to creae a vm with no hard drive: */
    if (selectedDiskSource() == SelectedDiskSource_Empty)
    {
        /* Ask user about disk-less machine unless that's the recommendation: */
        if (!m_fRecommendedNoDisk)
        {
            if (!msgCenter().confirmHardDisklessMachine(thisImp()))
                return false;
        }
    }
    else if (selectedDiskSource() == SelectedDiskSource_New)
    {
        /* Check if the path we will be using for hard drive creation exists: */
        const QString strMediumPath(fieldImp("mediumPath").toString());
        fResult = !QFileInfo(strMediumPath).exists();
        if (!fResult)
        {
            msgCenter().cannotOverwriteHardDiskStorage(strMediumPath, this);
            return fResult;
        }
        /* Check FAT size limitation of the host hard drive: */
        fResult = UIWizardNewVDPageBaseSizeLocation::checkFATSizeLimitation(fieldImp("mediumVariant").toULongLong(),
                                                             fieldImp("mediumPath").toString(),
                                                             fieldImp("mediumSize").toULongLong());
        if (!fResult)
        {
            msgCenter().cannotCreateHardDiskStorageInFAT(strMediumPath, this);
            return fResult;
        }
    }

    startProcessing();
    UIWizardNewVM *pWizard = wizardImp();
    if (pWizard)
    {
        if (selectedDiskSource() == SelectedDiskSource_New)
        {
            /* Try to create the hard drive:*/
            fResult = pWizard->createVirtualDisk();
            /*Don't show any error message here since UIWizardNewVM::createVirtualDisk already does so: */
            if (!fResult)
                return fResult;
        }

        fResult = pWizard->createVM();
        /* Try to delete the hard disk: */
        if (!fResult)
            pWizard->deleteVirtualDisk();
    }
    endProcessing();

    return fResult;
}

void UIWizardNewVMPageDisk::sltHandleSizeEditorChange()
{
    m_fUserSetSize = true;
}

void UIWizardNewVMPageDisk::setEnableNewDiskWidgets(bool fEnable)
{
    if (m_pMediumSizeEditor)
        m_pMediumSizeEditor->setEnabled(fEnable);
    if (m_pMediumSizeEditorLabel)
        m_pMediumSizeEditorLabel->setEnabled(fEnable);
    if (m_pFixedCheckBox)
        m_pFixedCheckBox->setEnabled(fEnable);
}

void UIWizardNewVMPageDisk::setVirtualDiskFromDiskCombo()
{
    AssertReturnVoid(m_pDiskSelector);
    UIWizardNewVM *pWizard = wizardImp();
    AssertReturnVoid(pWizard);
    pWizard->setVirtualDisk(m_pDiskSelector->id());
}
