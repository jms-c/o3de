/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/Document/MaterialDocumentRequestBus.h>
#include <Atom/RHI/Factory.h>
#include <Atom/Window/MaterialEditorWindowSettings.h>
#include <AtomToolsFramework/Document/AtomToolsDocumentRequestBus.h>
#include <AtomToolsFramework/Document/AtomToolsDocumentSystemRequestBus.h>
#include <AtomToolsFramework/Util/Util.h>
#include <AtomToolsFramework/Window/AtomToolsMainWindowNotificationBus.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzQtComponents/Components/StyleManager.h>
#include <AzQtComponents/Components/WindowDecorationWrapper.h>
#include <AzToolsFramework/API/EditorAssetSystemAPI.h>
#include <AzToolsFramework/API/EditorPythonRunnerRequestsBus.h>
#include <AzToolsFramework/PythonTerminal/ScriptTermDialog.h>
#include <Viewport/MaterialViewportWidget.h>
#include <Window/CreateMaterialDialog/CreateMaterialDialog.h>
#include <Window/HelpDialog/HelpDialog.h>
#include <Window/MaterialBrowserWidget.h>
#include <Window/MaterialEditorWindow.h>
#include <Window/MaterialInspector/MaterialInspector.h>
#include <Window/PerformanceMonitor/PerformanceMonitorWidget.h>
#include <Window/SettingsDialog/SettingsDialog.h>
#include <Window/ViewportSettingsInspector/ViewportSettingsInspector.h>

AZ_PUSH_DISABLE_WARNING(4251 4800, "-Wunknown-warning-option") // disable warnings spawned by QT
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QWindow>
AZ_POP_DISABLE_WARNING

namespace MaterialEditor
{
    MaterialEditorWindow::MaterialEditorWindow(QWidget* parent /* = 0 */)
        : AtomToolsFramework::AtomToolsMainWindow(parent)
    {
        resize(1280, 1024);

        // Among other things, we need the window wrapper to save the main window size, position, and state
        auto mainWindowWrapper =
            new AzQtComponents::WindowDecorationWrapper(AzQtComponents::WindowDecorationWrapper::OptionAutoTitleBarButtons);
        mainWindowWrapper->setGuest(this);
        mainWindowWrapper->enableSaveRestoreGeometry("O3DE", "MaterialEditor", "mainWindowGeometry");

        // set the style sheet for RPE highlighting and other styling
        AzQtComponents::StyleManager::setStyleSheet(this, QStringLiteral(":/MaterialEditor.qss"));

        QApplication::setWindowIcon(QIcon(":/Icons/materialeditor.svg"));

        AZ::Name apiName = AZ::RHI::Factory::Get().GetName();
        if (!apiName.IsEmpty())
        {
            QString title = QString{ "%1 (%2)" }.arg(QApplication::applicationName()).arg(apiName.GetCStr());
            setWindowTitle(title);
        }
        else
        {
            AZ_Assert(false, "Render API name not found");
            setWindowTitle(QApplication::applicationName());
        }

        setObjectName("MaterialEditorWindow");

        m_toolBar = new MaterialEditorToolBar(this);
        m_toolBar->setObjectName("ToolBar");
        addToolBar(m_toolBar);

        CreateMenu();
        CreateTabBar();

        m_materialViewport = new MaterialViewportWidget(centralWidget());
        m_materialViewport->setObjectName("Viewport");
        m_materialViewport->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
        centralWidget()->layout()->addWidget(m_materialViewport);

        AddDockWidget("Asset Browser", new MaterialBrowserWidget, Qt::BottomDockWidgetArea, Qt::Vertical);
        AddDockWidget("Inspector", new MaterialInspector, Qt::RightDockWidgetArea, Qt::Horizontal);
        AddDockWidget("Viewport Settings", new ViewportSettingsInspector, Qt::LeftDockWidgetArea, Qt::Horizontal);
        AddDockWidget("Performance Monitor", new PerformanceMonitorWidget, Qt::RightDockWidgetArea, Qt::Horizontal);
        AddDockWidget("Python Terminal", new AzToolsFramework::CScriptTermDialog, Qt::BottomDockWidgetArea, Qt::Horizontal);

        SetDockWidgetVisible("Viewport Settings", false);
        SetDockWidgetVisible("Performance Monitor", false);
        SetDockWidgetVisible("Python Terminal", false);

        // Restore geometry and show the window
        mainWindowWrapper->showFromSettings();

        // Restore additional state for docked windows
        auto windowSettings = AZ::UserSettings::CreateFind<MaterialEditorWindowSettings>(
            AZ::Crc32("MaterialEditorWindowSettings"), AZ::UserSettings::CT_GLOBAL);

        if (!windowSettings->m_mainWindowState.empty())
        {
            QByteArray windowState(windowSettings->m_mainWindowState.data(), static_cast<int>(windowSettings->m_mainWindowState.size()));
            m_advancedDockManager->restoreState(windowState);
        }

        AtomToolsFramework::AtomToolsDocumentNotificationBus::Handler::BusConnect();
        OnDocumentOpened(AZ::Uuid::CreateNull());
    }

    MaterialEditorWindow::~MaterialEditorWindow()
    {
        AtomToolsFramework::AtomToolsDocumentNotificationBus::Handler::BusDisconnect();
    }

    
    void MaterialEditorWindow::ResizeViewportRenderTarget(uint32_t width, uint32_t height)
    {
        QSize requestedViewportSize = QSize(width, height) / devicePixelRatioF();
        QSize currentViewportSize = m_materialViewport->size();
        QSize offset = requestedViewportSize - currentViewportSize;
        QSize requestedWindowSize = size() + offset;
        resize(requestedWindowSize);

        AZ_Assert(
            m_materialViewport->size() == requestedViewportSize,
            "Resizing the window did not give the expected viewport size. Requested %d x %d but got %d x %d.",
            requestedViewportSize.width(), requestedViewportSize.height(), m_materialViewport->size().width(),
            m_materialViewport->size().height());

        QSize newDeviceSize = m_materialViewport->size();
        AZ_Warning(
            "Material Editor", static_cast<uint32_t>(newDeviceSize.width()) == width && static_cast<uint32_t>(newDeviceSize.height()) == height,
            "Resizing the window did not give the expected frame size. Requested %d x %d but got %d x %d.", width, height,
            newDeviceSize.width(), newDeviceSize.height());
    }

    void MaterialEditorWindow::LockViewportRenderTargetSize(uint32_t width, uint32_t height)
    {
        m_materialViewport->LockRenderTargetSize(width, height);
    }

    void MaterialEditorWindow::UnlockViewportRenderTargetSize()
    {
        m_materialViewport->UnlockRenderTargetSize();
    }

    void MaterialEditorWindow::closeEvent(QCloseEvent* closeEvent)
    {
        bool didClose = true;
        AtomToolsFramework::AtomToolsDocumentSystemRequestBus::BroadcastResult(didClose, &AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseAllDocuments);
        if (!didClose)
        {
            closeEvent->ignore();
            return;
        }

        // Capture docking state before shutdown
        auto windowSettings = AZ::UserSettings::CreateFind<MaterialEditorWindowSettings>(
            AZ::Crc32("MaterialEditorWindowSettings"), AZ::UserSettings::CT_GLOBAL);

        QByteArray windowState = m_advancedDockManager->saveState();
        windowSettings->m_mainWindowState.assign(windowState.begin(), windowState.end());

        AtomToolsFramework::AtomToolsMainWindowNotificationBus::Broadcast(
            &AtomToolsFramework::AtomToolsMainWindowNotifications::OnMainWindowClosing);
    }

    void MaterialEditorWindow::OnDocumentOpened(const AZ::Uuid& documentId)
    {
        bool isOpen = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(isOpen, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::IsOpen);
        bool isSavable = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(isSavable, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::IsSavable);
        bool isModified = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(isModified, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::IsModified);
        bool canUndo = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(canUndo, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::CanUndo);
        bool canRedo = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(canRedo, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::CanRedo);
        AZStd::string absolutePath;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(absolutePath, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::GetAbsolutePath);
        AZStd::string filename;
        AzFramework::StringFunc::Path::GetFullFileName(absolutePath.c_str(), filename);

        // Update UI to display the new document
        if (!documentId.IsNull() && isOpen)
        {
            // Create a new tab for the document ID and assign it's label to the file name of the document.
            AddTabForDocumentId(documentId, filename, absolutePath, [this]{
                // The tab widget requires a dummy page per tab
                auto contentWidget = new QWidget(centralWidget());
                contentWidget->setContentsMargins(0, 0, 0, 0);
                contentWidget->setFixedSize(0, 0);
                return contentWidget;
            });
        }

        UpdateTabForDocumentId(documentId, filename, absolutePath, isModified);

        const bool hasTabs = m_tabWidget->count() > 0;

        // Update menu options
        m_actionNew->setEnabled(true);
        m_actionOpen->setEnabled(true);
        m_actionOpenRecent->setEnabled(false);
        m_actionClose->setEnabled(hasTabs);
        m_actionCloseAll->setEnabled(hasTabs);
        m_actionCloseOthers->setEnabled(hasTabs);

        m_actionSave->setEnabled(isOpen && isSavable);
        m_actionSaveAsCopy->setEnabled(isOpen && isSavable);
        m_actionSaveAsChild->setEnabled(isOpen);
        m_actionSaveAll->setEnabled(hasTabs);

        m_actionExit->setEnabled(true);

        m_actionUndo->setEnabled(canUndo);
        m_actionRedo->setEnabled(canRedo);
        m_actionSettings->setEnabled(true);

        m_actionAssetBrowser->setEnabled(true);
        m_actionInspector->setEnabled(true);
        m_actionConsole->setEnabled(false);
        m_actionPythonTerminal->setEnabled(true);
        m_actionPerfMonitor->setEnabled(true);
        m_actionViewportSettings->setEnabled(true);
        m_actionPreviousTab->setEnabled(m_tabWidget->count() > 1);
        m_actionNextTab->setEnabled(m_tabWidget->count() > 1);

        m_actionAbout->setEnabled(false);

        activateWindow();
        raise();

        const QString documentPath = GetDocumentPath(documentId);
        if (!documentPath.isEmpty())
        {
            SetStatusMessage(tr("Document opened: %1").arg(documentPath));
        }
    }

    void MaterialEditorWindow::OnDocumentClosed(const AZ::Uuid& documentId)
    {
        RemoveTabForDocumentId(documentId);
        SetStatusMessage(tr("Document closed: %1").arg(GetDocumentPath(documentId)));
    }

    void MaterialEditorWindow::OnDocumentModified(const AZ::Uuid& documentId)
    {
        bool isModified = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(isModified, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::IsModified);
        AZStd::string absolutePath;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(absolutePath, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::GetAbsolutePath);
        AZStd::string filename;
        AzFramework::StringFunc::Path::GetFullFileName(absolutePath.c_str(), filename);
        UpdateTabForDocumentId(documentId, filename, absolutePath, isModified);
    }

    void MaterialEditorWindow::OnDocumentUndoStateChanged(const AZ::Uuid& documentId)
    {
        if (documentId == GetDocumentIdFromTab(m_tabWidget->currentIndex()))
        {
            bool canUndo = false;
            AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(canUndo, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::CanUndo);
            bool canRedo = false;
            AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(canRedo, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::CanRedo);
            m_actionUndo->setEnabled(canUndo);
            m_actionRedo->setEnabled(canRedo);
        }
    }

    void MaterialEditorWindow::OnDocumentSaved(const AZ::Uuid& documentId)
    {
        bool isModified = false;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(isModified, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::IsModified);
        AZStd::string absolutePath;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(absolutePath, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::GetAbsolutePath);
        AZStd::string filename;
        AzFramework::StringFunc::Path::GetFullFileName(absolutePath.c_str(), filename);
        UpdateTabForDocumentId(documentId, filename, absolutePath, isModified);
        SetStatusMessage(tr("Document saved: %1").arg(GetDocumentPath(documentId)));
    }

    void MaterialEditorWindow::CreateMenu()
    {
        Base::CreateMenu();

        // Generating the main menu manually because it's easier and we will have some dynamic or data driven entries
        m_menuFile = menuBar()->addMenu("&File");

        m_actionNew = m_menuFile->addAction("&New...", [this]() {
            CreateMaterialDialog createDialog(this);
            createDialog.adjustSize();

            if (createDialog.exec() == QDialog::Accepted &&
                !createDialog.m_materialFileInfo.absoluteFilePath().isEmpty() &&
                !createDialog.m_materialTypeFileInfo.absoluteFilePath().isEmpty())
            {
                AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CreateDocumentFromFile,
                    createDialog.m_materialTypeFileInfo.absoluteFilePath().toUtf8().constData(),
                    createDialog.m_materialFileInfo.absoluteFilePath().toUtf8().constData());
            }
        }, QKeySequence::New);

        m_actionOpen = m_menuFile->addAction("&Open...", [this]() {
            const AZStd::vector<AZ::Data::AssetType> assetTypes = { azrtti_typeid<AZ::RPI::MaterialAsset>() };
            const AZStd::string filePath = AtomToolsFramework::GetOpenFileInfo(assetTypes).absoluteFilePath().toUtf8().constData();
            if (!filePath.empty())
            {
                AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::OpenDocument, filePath);
            }
        }, QKeySequence::Open);

        m_actionOpenRecent = m_menuFile->addAction("Open &Recent");

        m_menuFile->addSeparator();

        m_actionSave = m_menuFile->addAction("&Save", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            bool result = false;
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::BroadcastResult(result, &AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::SaveDocument, documentId);
            if (!result)
            {
                SetStatusError(tr("Document save failed: %1").arg(GetDocumentPath(documentId)));
            }
        }, QKeySequence::Save);

        m_actionSaveAsCopy = m_menuFile->addAction("Save &As...", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            const QString documentPath = GetDocumentPath(documentId);

            bool result = false;
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::BroadcastResult(result, &AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::SaveDocumentAsCopy,
                documentId, AtomToolsFramework::GetSaveFileInfo(documentPath).absoluteFilePath().toUtf8().constData());
            if (!result)
            {
                SetStatusError(tr("Document save failed: %1").arg(GetDocumentPath(documentId)));
            }
        }, QKeySequence::SaveAs);

        m_actionSaveAsChild = m_menuFile->addAction("Save As &Child...", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            const QString documentPath = GetDocumentPath(documentId);

            bool result = false;
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::BroadcastResult(result, &AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::SaveDocumentAsChild,
                documentId, AtomToolsFramework::GetSaveFileInfo(documentPath).absoluteFilePath().toUtf8().constData());
            if (!result)
            {
                SetStatusError(tr("Document save failed: %1").arg(GetDocumentPath(documentId)));
            }
        });

        m_actionSaveAll = m_menuFile->addAction("Save A&ll", [this]() {
            bool result = false;
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::BroadcastResult(result, &AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::SaveAllDocuments);
            if (!result)
            {
                SetStatusError(tr("Document save all failed"));
            }
        });

        m_menuFile->addSeparator();

        m_actionClose = m_menuFile->addAction("&Close", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseDocument, documentId);
        }, QKeySequence::Close);

        m_actionCloseAll = m_menuFile->addAction("Close All", [this]() {
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseAllDocuments);
        });

        m_actionCloseOthers = m_menuFile->addAction("Close Others", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseAllDocumentsExcept, documentId);
        });

        m_menuFile->addSeparator();

        m_menuFile->addAction("Run &Python...", [this]() {
            const QString script = QFileDialog::getOpenFileName(this, "Run Script", QString(), QString("*.py"));
            if (!script.isEmpty())
            {
                AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(&AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByFilename, script.toUtf8().constData());
            }
        });

        m_menuFile->addSeparator();

        m_actionExit = m_menuFile->addAction("E&xit", [this]() {
            close();
        }, QKeySequence::Quit);

        m_menuEdit = menuBar()->addMenu("&Edit");

        m_actionUndo = m_menuEdit->addAction("&Undo", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            bool result = false;
            AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(result, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::Undo);
            if (!result)
            {
                SetStatusError(tr("Document undo failed: %1").arg(GetDocumentPath(documentId)));
            }
        }, QKeySequence::Undo);

        m_actionRedo = m_menuEdit->addAction("&Redo", [this]() {
            const AZ::Uuid documentId = GetDocumentIdFromTab(m_tabWidget->currentIndex());
            bool result = false;
            AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(result, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Events::Redo);
            if (!result)
            {
                SetStatusError(tr("Document redo failed: %1").arg(GetDocumentPath(documentId)));
            }
        }, QKeySequence::Redo);

        m_menuEdit->addSeparator();

        m_actionSettings = m_menuEdit->addAction("&Settings...", [this]() {
            SettingsDialog dialog(this);
            dialog.exec();
        }, QKeySequence::Preferences);
        m_actionSettings->setEnabled(true);

        m_menuView = menuBar()->addMenu("&View");

        m_actionAssetBrowser = m_menuView->addAction("&Asset Browser", [this]() {
            const AZStd::string label = "Asset Browser";
            SetDockWidgetVisible(label, !IsDockWidgetVisible(label));
        });

        m_actionInspector = m_menuView->addAction("&Inspector", [this]() {
            const AZStd::string label = "Inspector";
            SetDockWidgetVisible(label, !IsDockWidgetVisible(label));
        });

        m_actionConsole = m_menuView->addAction("&Console", [this]() {
        });

        m_actionPythonTerminal = m_menuView->addAction("Python &Terminal", [this]() {
            const AZStd::string label = "Python Terminal";
            SetDockWidgetVisible(label, !IsDockWidgetVisible(label));
        });

        m_actionPerfMonitor = m_menuView->addAction("Performance &Monitor", [this]() {
            const AZStd::string label = "Performance Monitor";
            SetDockWidgetVisible(label, !IsDockWidgetVisible(label));
        });

        m_actionViewportSettings = m_menuView->addAction("Viewport Settings", [this]() {
            const AZStd::string label = "Viewport Settings";
            SetDockWidgetVisible(label, !IsDockWidgetVisible(label));
        });

        m_menuView->addSeparator();

        m_actionPreviousTab = m_menuView->addAction("&Previous Tab", [this]() {
            SelectPreviousTab();
        }, Qt::CTRL | Qt::SHIFT | Qt::Key_Tab); //QKeySequence::PreviousChild is mapped incorrectly in Qt

        m_actionNextTab = m_menuView->addAction("&Next Tab", [this]() {
            SelectNextTab();
        }, Qt::CTRL | Qt::Key_Tab); //QKeySequence::NextChild works as expected but mirroring Previous

        m_menuHelp = menuBar()->addMenu("&Help");

        m_actionHelp = m_menuHelp->addAction("&Help...", [this]() {
            HelpDialog dialog(this);
            dialog.exec();
        });

        m_actionAbout = m_menuHelp->addAction("&About...", [this]() {
        });
    }

    void MaterialEditorWindow::CreateTabBar()
    {
        Base::CreateTabBar();

        // This signal will be triggered whenever a tab is added, removed, selected, clicked, dragged
        // When the last tab is removed tabIndex will be -1 and the document ID will be null
        // This should automatically clear the active document
        connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int tabIndex) {
            const AZ::Uuid documentId = GetDocumentIdFromTab(tabIndex);
            AtomToolsFramework::AtomToolsDocumentNotificationBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentOpened, documentId);
        });

        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int tabIndex) {
            const AZ::Uuid documentId = GetDocumentIdFromTab(tabIndex);
            AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseDocument, documentId);
        });
    }

    QString MaterialEditorWindow::GetDocumentPath(const AZ::Uuid& documentId) const
    {
        AZStd::string absolutePath;
        AtomToolsFramework::AtomToolsDocumentRequestBus::EventResult(absolutePath, documentId, &AtomToolsFramework::AtomToolsDocumentRequestBus::Handler::GetAbsolutePath);
        return absolutePath.c_str();
    }

    void MaterialEditorWindow::OpenTabContextMenu()
    {
        const QTabBar* tabBar = m_tabWidget->tabBar();
        const QPoint position = tabBar->mapFromGlobal(QCursor::pos());
        const int clickedTabIndex = tabBar->tabAt(position);
        const int currentTabIndex = tabBar->currentIndex();
        if (clickedTabIndex >= 0)
        {
            QMenu tabMenu;
            const QString selectActionName = (currentTabIndex == clickedTabIndex) ? "Select in Browser" : "Select";
            tabMenu.addAction(selectActionName, [this, clickedTabIndex]() {
                const AZ::Uuid documentId = GetDocumentIdFromTab(clickedTabIndex);
                AtomToolsFramework::AtomToolsDocumentNotificationBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentNotificationBus::Events::OnDocumentOpened, documentId);
            });
            tabMenu.addAction("Close", [this, clickedTabIndex]() {
                const AZ::Uuid documentId = GetDocumentIdFromTab(clickedTabIndex);
                AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseDocument, documentId);
            });
            auto closeOthersAction = tabMenu.addAction("Close Others", [this, clickedTabIndex]() {
                const AZ::Uuid documentId = GetDocumentIdFromTab(clickedTabIndex);
                AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Broadcast(&AtomToolsFramework::AtomToolsDocumentSystemRequestBus::Events::CloseAllDocumentsExcept, documentId);
            });
            closeOthersAction->setEnabled(tabBar->count() > 1);
            tabMenu.exec(QCursor::pos());
        }
    }
} // namespace MaterialEditor

#include <Window/moc_MaterialEditorWindow.cpp>
