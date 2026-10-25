#pragma once

#include "common.h"

#include <QIcon>
#include <QMainWindow>
#include <QString>

#include <memory>
#include <deque>
#include <set>
#include <vector>

class QAction;
class QActionGroup;
class QApplication;
class QByteArray;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QProgressBar;
class QShowEvent;
class QSplitter;
class QSystemTrayIcon;
class QTableWidget;
class QToolBar;
class QTreeWidget;
class QWidget;
class ChromeBridge;

enum class UiLanguage;

// ---------------------------------------------------------------------------
// Main window (Qt Widgets)
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Queues a URL, exactly like the clipboard / drop paths (startNow forces
    // the download to begin regardless of the start-on-add setting).
    void addUrlFromText(const std::wstring& url, bool notify,
                        bool startNow = false);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message,
                     qintptr* result) override;

private:
    enum class SortMode {
        AdditionOrder,
        FileName,
        Size,
        Status,
        TimeLeft,
        TransferRate,
        LastTryDate,
        Description,
        SavePath,
        Referer,
        ParentPage,
    };

    void buildUi();
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildCategories();
    void buildDownloadTable();
    void buildStatusBar();
    void setupTray();

    void refresh(bool force);
    void rebuildTable(const std::vector<std::shared_ptr<DownloadItem>>& items);
    void updateTableRow(int row, const std::shared_ptr<DownloadItem>& item);
    void updateActions(const std::vector<std::shared_ptr<DownloadItem>>& items);
    void updateStatus(const std::vector<std::shared_ptr<DownloadItem>>& items);
    bool matchesCategory(DownloadItem& item) const;

    uint64_t selectedId() const;
    void selectId(uint64_t id);
    std::shared_ptr<DownloadItem> selectedItem() const;

    void showAddDialog();
    void addFromClipboard();
    void startSelected();
    void stopSelected();
    void redownloadSelected();
    void showFindDialog();
    void findNextMatch();
    void setSpeedLimit(int kb);
    void setSortMode(SortMode mode);
    void sortVisible(std::vector<std::shared_ptr<DownloadItem>>& items) const;
    void setLanguage(UiLanguage language);
    void retranslateUi();
    void removeSelected(bool deleteFile);
    void removeCompleted();
    void openSelectedFile();
    void openSelectedFolder();
    void copySelectedUrl();
    void showOptions();
    void showHelp();
    void toggleCategories(bool visible);
    void hideToTray();
    void requestQuit();
    void restoreFromTray();
    void enqueueChromeDownload(ChromeDownloadRequest request);
    void showNextChromeDownload();
    void detectCompletedDownloads(
        const std::vector<std::shared_ptr<DownloadItem>>& items);
    void showNextDownloadComplete();

    QTreeWidget* categories_ = nullptr;
    QWidget* categoriesPanel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QMenu* speedMenu_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;

    QAction* actAdd_ = nullptr;
    QAction* actAddClipboard_ = nullptr;
    QAction* actResume_ = nullptr;
    QAction* actStop_ = nullptr;
    QAction* actPauseAll_ = nullptr;
    QAction* actRedownload_ = nullptr;
    QAction* actStartAll_ = nullptr;
    QAction* actStopAll_ = nullptr;
    QAction* actStartQueue_ = nullptr;
    QAction* actStopQueue_ = nullptr;
    QAction* actDelete_ = nullptr;
    QAction* actDeleteFile_ = nullptr;
    QAction* actDeleteCompleted_ = nullptr;
    QAction* actOpen_ = nullptr;
    QAction* actOpenFolder_ = nullptr;
    QAction* actCopyUrl_ = nullptr;
    QAction* actSelectAll_ = nullptr;
    QAction* actOptions_ = nullptr;
    QAction* actScheduler_ = nullptr;
    QAction* actTellFriend_ = nullptr;
    QAction* actRefresh_ = nullptr;
    QAction* actFind_ = nullptr;
    QAction* actFindNext_ = nullptr;
    QAction* actSpeedOff_ = nullptr;
    QAction* actLanguageEnglish_ = nullptr;
    QAction* actLanguageArabic_ = nullptr;
    QAction* actViewToolbar_ = nullptr;
    QAction* actViewStatus_ = nullptr;
    QAction* actViewCategories_ = nullptr;
    QAction* actHelp_ = nullptr;
    QAction* actAbout_ = nullptr;
    QAction* actRegister_ = nullptr;
    QAction* actRegistrationInfo_ = nullptr;
    QAction* actExit_ = nullptr;

    QString category_ = QStringLiteral("all");
    std::vector<uint64_t> rowIds_;
    QString findText_;
    int findRow_ = -1;
    SortMode sort_ = SortMode::AdditionOrder;
    std::unique_ptr<ChromeBridge> bridge_;
    std::deque<ChromeDownloadRequest> chromeQueue_;
    std::deque<std::shared_ptr<DownloadItem>> completeQueue_;
    std::set<uint64_t> announcedComplete_;
    bool chromeDialogOpen_ = false;
    bool completeDialogOpen_ = false;
    bool completionBaselineDone_ = false;
    bool trayHintShown_ = false;
    bool quitRequested_ = false;
    bool closing_ = false;
};

// ---------------------------------------------------------------------------
// Application theme (ui_main.cpp)
// ---------------------------------------------------------------------------
void applyApplicationTheme(QApplication& app);
// Paints the native Windows title bar dark to match the in-app theme.
void applyDarkTitleBar(QWidget* window);
QIcon applicationIcon();

// ---------------------------------------------------------------------------
// Modal dialogs (ui_dialogs.cpp)
// ---------------------------------------------------------------------------
bool ShowAddDownloadDialogQt(QWidget* owner, const Config& cfg,
                             AddDownloadResult& result);
bool ShowDownloadFileInfoQt(QWidget* owner, const Config& cfg,
                            DownloadInfoResult& result,
                            bool floating = false);
// Opens the IDM-style "Download progress" window for an item that just
// started. Non-modal: the caller keeps running while the window polls the
// item's counters, and the window closes itself when the transfer ends.
void ShowDownloadProgressQt(QWidget* owner,
                            const std::shared_ptr<DownloadItem>& item);
// Closes the progress window of one item immediately. Used before the
// "Download complete" window is shown so it does not linger on screen.
void CloseDownloadProgressQt(uint64_t downloadId);
// Shows the "Download complete" window once a file finishes. Returns true when
// the user ticked "Don't show this dialog again".
bool ShowDownloadCompleteQt(QWidget* owner, const std::wstring& url,
                            const std::wstring& savePath, uint64_t bytes);
bool ShowOptionsDialogQt(QWidget* owner, Config& cfg);
void ShowAboutDialogQt(QWidget* owner);
bool BrowseForFolderQt(QWidget* owner, const std::wstring& title,
                       std::wstring& out);
