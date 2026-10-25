#include "ui_qt.h"
#include "bridge.h"
#include "localization.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// Small conversion helpers
// ---------------------------------------------------------------------------
QString ToQ(const std::wstring& text) {
    return QString::fromWCharArray(text.c_str(), (int)text.size());
}

std::wstring ToW(const QString& text) {
    return text.toStdWString();
}

QString AppTitle() {
    return QString::fromWCharArray(APP_NAME);
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
QIcon FromPixmap(const QPixmap& pixmap) {
    QIcon icon;
    icon.addPixmap(pixmap);
    icon.addPixmap(pixmap.scaled(32, 32, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation));
    return icon;
}

// Toolbar icons use the embedded Fluent System Icons font (MIT licence). The
// glyph outlines are filled through QPainterPath so they stay sharp at every
// DPI and can carry an accent colour per action.
constexpr ushort IconGlyphAdd = 0xF10A;
constexpr ushort IconGlyphPlay = 0xF606;
constexpr ushort IconGlyphDismiss = 0xF36A;
constexpr ushort IconGlyphHand = 0xE6DC;
constexpr ushort IconGlyphDelete = 0xF34D;
constexpr ushort IconGlyphBroom = 0xF202;
constexpr ushort IconGlyphSettings = 0xF6AA;
constexpr ushort IconGlyphClock = 0xF2DE;
constexpr ushort IconGlyphDownload = 0xF151;
constexpr ushort IconGlyphStop = 0xF72B;
constexpr ushort IconGlyphPeople = 0xF5A9;
constexpr ushort IconGlyphArrowClockwise = 0xF13E;
constexpr ushort IconGlyphPause = 0xF5A2;

QString FluentIconFamily() {
    static const QString family = [] {
        const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/FluentSystemIcons-Subset.ttf"));
        if (id < 0) return QString();
        const QStringList families =
            QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString() : families.first();
    }();
    return family;
}

QPixmap FluentPixmap(ushort codepoint, const QColor& color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    const QString family = FluentIconFamily();
    if (family.isEmpty()) return pixmap;

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    QFont font(family);
    font.setPixelSize(size);
    QPainterPath glyph;
    glyph.addText(QPointF(0, 0), font, QString(QChar(codepoint)));

    const QRectF bounds = glyph.boundingRect();
    if (bounds.isEmpty()) return pixmap;

    // Normalise the glyph to a common optical box and centre it, since glyph
    // advances and bearings differ from one icon to another.
    const qreal target = size * 0.74;
    const qreal scale = target / qMax(bounds.width(), bounds.height());
    QTransform transform;
    transform.translate(size / 2.0, size / 2.0);
    transform.scale(scale, scale);
    transform.translate(-bounds.center().x(), -bounds.center().y());

    QLinearGradient gradient(0.0, size * 0.15, 0.0, size * 0.85);
    gradient.setColorAt(0.0, color.lighter(116));
    gradient.setColorAt(1.0, color.darker(106));

    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawPath(transform.map(glyph));
    return pixmap;
}

QIcon FluentIcon(ushort codepoint, const QColor& color) {
    QIcon icon;
    const QColor disabled(104, 114, 128);
    for (int size : { 24, 32, 48, 64, 96, 128 }) {
        icon.addPixmap(FluentPixmap(codepoint, color, size));
        icon.addPixmap(FluentPixmap(codepoint, disabled, size),
                       QIcon::Disabled);
    }
    return icon;
}

QIcon IconAddUrl() {
    return FluentIcon(IconGlyphAdd, QColor(76, 175, 80));
}

QIcon IconResume() {
    return FluentIcon(IconGlyphPlay, QColor(67, 160, 71));
}

QIcon IconRedownload() {
    return FluentIcon(IconGlyphArrowClockwise, QColor(74, 144, 226));
}

QIcon IconPause() {
    return FluentIcon(IconGlyphPause, QColor(230, 162, 60));
}

QIcon IconStop() {
    return FluentIcon(IconGlyphDismiss, QColor(214, 74, 64));
}

QIcon IconHand() {
    return FluentIcon(IconGlyphHand, QColor(186, 197, 210));
}

QIcon IconDelete() {
    return FluentIcon(IconGlyphDelete, QColor(170, 178, 188));
}

QIcon IconDeleteCompleted() {
    return FluentIcon(IconGlyphBroom, QColor(199, 91, 155));
}

QIcon IconOptions() {
    return FluentIcon(IconGlyphSettings, QColor(175, 188, 204));
}

QIcon IconScheduler() {
    return FluentIcon(IconGlyphClock, QColor(230, 162, 60));
}

QIcon IconStartQueue() {
    return FluentIcon(IconGlyphDownload, QColor(74, 144, 226));
}

QIcon IconStopQueue() {
    return FluentIcon(IconGlyphStop, QColor(199, 91, 155));
}

QIcon IconTellFriend() {
    return FluentIcon(IconGlyphPeople, QColor(74, 144, 226));
}

QIcon FolderIcon() {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);

    const QColor base(93, 128, 170);
    p.setPen(Qt::NoPen);
    p.setBrush(base.darker(135));
    p.drawRoundedRect(QRectF(1, 4, 14, 11), 1.2, 1.2);
    p.setBrush(base);
    p.drawRoundedRect(QRectF(2, 5.5, 12, 8.5), 1.2, 1.2);
    p.setBrush(base.lighter(120));
    p.drawRoundedRect(QRectF(2, 5.5, 12, 2), 1.2, 1.2);
    return FromPixmap(pixmap);
}

QColor ExtensionColor(const QString& extension) {
    static const QStringList archives = { "zip", "rar", "7z", "tar", "gz",
                                          "bz2", "xz", "zst" };
    static const QStringList documents = { "pdf", "doc", "docx", "xls", "xlsx",
                                           "ppt", "pptx", "txt", "rtf", "csv",
                                           "epub", "md" };
    static const QStringList music = { "mp3", "wav", "flac", "aac", "ogg",
                                       "m4a", "wma" };
    static const QStringList programs = { "exe", "msi", "apk", "jar", "deb",
                                          "rpm" };
    static const QStringList video = { "mp4", "mkv", "avi", "mov", "wmv",
                                       "flv", "webm", "m4v", "mpg", "mpeg",
                                       "ts" };

    if (archives.contains(extension)) return QColor(199, 123, 58);
    if (documents.contains(extension)) return QColor(76, 132, 200);
    if (music.contains(extension)) return QColor(56, 160, 150);
    if (programs.contains(extension)) return QColor(76, 175, 80);
    if (video.contains(extension)) return QColor(155, 100, 200);
    if (extension == QStringLiteral("iso")) return QColor(126, 96, 190);
    return QColor(130, 142, 158);
}

QIcon FileTypeIcon(const QString& fileName) {
    const QString extension = QFileInfo(fileName).suffix().toLower();
    const QColor color = ExtensionColor(extension);

    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);

    QPainterPath page;
    page.moveTo(3, 1);
    page.lineTo(10, 1);
    page.lineTo(14, 5);
    page.lineTo(14, 15);
    page.lineTo(3, 15);
    page.closeSubpath();

    QPainterPath fold;
    fold.moveTo(10, 1);
    fold.lineTo(10, 5);
    fold.lineTo(14, 5);
    fold.closeSubpath();

    p.setPen(QPen(QColor(40, 48, 60), 0.8));
    p.setBrush(QColor(228, 234, 242));
    p.drawPath(page);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(178, 190, 205));
    p.drawPath(fold);

    p.setBrush(color);
    p.drawRoundedRect(QRectF(5, 8.5, 7, 5), 1, 1);
    p.setBrush(QColor(255, 255, 255, 225));
    p.drawRect(QRectF(6.2, 10, 4.6, 0.8));
    p.drawRect(QRectF(6.2, 11.6, 3.2, 0.8));
    return FromPixmap(pixmap);
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
QString StateLabel(DownloadState state) {
    switch (state) {
        case DownloadState::Queued:      return Tr("Queued");
        case DownloadState::Connecting:  return Tr("Connecting...");
        case DownloadState::Downloading: return Tr("Downloading");
        case DownloadState::Paused:      return Tr("Paused");
        case DownloadState::Complete:    return Tr("Complete");
        case DownloadState::Error:       return Tr("Error");
        case DownloadState::Cancelled:   return Tr("Cancelled");
    }
    return Tr("Unknown");
}

QString LastTryText(const DownloadItem& item) {
    uint64_t stamp = item.finishedAt.load();
    if (stamp == 0) stamp = item.startedAt.load();
    if (stamp == 0) return QString();
    const QDateTime when = QDateTime::fromMSecsSinceEpoch((qint64)stamp);
    if (CurrentLanguage() == UiLanguage::Arabic) {
        return QLocale(QLocale::Arabic).toString(when,
                                                 QStringLiteral("d MMM HH:mm:ss"));
    }
    return when.toString(QStringLiteral("MMM d HH:mm:ss"));
}

QString TimeLeftText(const DownloadItem& item) {
    if (!item.IsRunning()) return QString();
    const double speed = item.speed.load();
    const uint64_t total = item.totalBytes.load();
    const uint64_t done = item.downloadedBytes.load();
    if (speed <= 0.0 || total == 0 || done >= total) return QString();
    const uint64_t seconds = (uint64_t)std::ceil((double)(total - done) / speed);
    return ToQ(FormatDuration(seconds));
}

// Stores the English source on the action so retranslateUi() can update it in
// place when the user switches languages.
void SetActionSource(QAction* action, const char* text,
                     const char* iconText = nullptr) {
    action->setProperty("enText", QString::fromUtf8(text));
    action->setText(Tr(text));
    if (iconText) {
        action->setProperty("enIconText", QString::fromUtf8(iconText));
        action->setIconText(Tr(iconText));
    }
}

void ApplyDarkTitleBar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    using SetAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttribute =
        (SetAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (setAttribute) {
        BOOL enabled = TRUE;
        if (FAILED(setAttribute(hwnd, 20, &enabled, sizeof(enabled)))) {
            setAttribute(hwnd, 19, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(dwm);
}

// ---------------------------------------------------------------------------
// Dark theme
// ---------------------------------------------------------------------------
const char* const kStyleSheet = R"QSS(
QWidget {
    color: #e4e8ee;
}
QMainWindow, QDialog {
    background: #1e252f;
}
QMenuBar {
    background: #171c24;
    color: #e4e8ee;
    padding: 2px 4px;
    border: none;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: 2px;
}
QMenuBar::item:selected {
    background: #384556;
}
QMenuBar::item:pressed {
    background: #386ea8;
}
QMenu {
    background: #2d3643;
    border: 1px solid #3a4554;
    padding: 4px;
}
QMenu::item {
    padding: 6px 30px 6px 26px;
    background: transparent;
    border-radius: 2px;
}
QMenu::item:selected {
    background: #386ea8;
    color: #ffffff;
}
QMenu::item:disabled {
    color: #6c7684;
}
QMenu::separator {
    height: 1px;
    background: #3a4554;
    margin: 4px 8px;
}
QToolBar#mainToolbar {
    background: #252d39;
    border: none;
    border-bottom: 1px solid #3a4554;
    padding: 3px 4px;
    spacing: 1px;
}
QToolBar#mainToolbar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    color: #e4e8ee;
    padding: 2px 5px;
    margin: 0 1px;
    min-width: 56px;
    max-width: 96px;
    font-size: 8pt;
}
QToolBar#mainToolbar QToolButton:hover {
    background: #384556;
}
QToolBar#mainToolbar QToolButton:pressed {
    background: #386ea8;
}
QToolBar#mainToolbar QToolButton:disabled {
    color: #6c7684;
}
QToolBar#mainToolbar::separator {
    background: #3a4554;
    width: 1px;
    margin: 7px 4px;
}
QSplitter::handle {
    background: #3a4554;
}
QWidget#categoriesHeader {
    background: #2d3643;
    border-bottom: 1px solid #3a4554;
}
QLabel#categoriesTitle {
    color: #e4e8ee;
    font-weight: bold;
}
QToolButton#categoriesClose {
    background: transparent;
    border: none;
    color: #96a0ae;
    font-weight: bold;
    padding: 0 4px;
}
QToolButton#categoriesClose:hover {
    color: #ffffff;
    background: #4a5568;
    border-radius: 2px;
}
QTreeWidget#categoriesTree {
    background: #1e252f;
    border: none;
    outline: none;
    color: #e4e8ee;
}
QTreeWidget#categoriesTree::item {
    height: 22px;
    padding-left: 2px;
}
QTreeWidget#categoriesTree::item:selected {
    background: #386ea8;
    color: #ffffff;
}
QTreeWidget#categoriesTree::item:hover {
    background: #2d3643;
}
QTableWidget#downloadTable {
    background: #252d39;
    alternate-background-color: #252d39;
    color: #e4e8ee;
    gridline-color: #3a4554;
    border: 1px solid #3a4554;
    selection-background-color: #386ea8;
    selection-color: #ffffff;
    outline: none;
}
QTableWidget#downloadTable::item {
    padding: 2px 4px;
}
QHeaderView::section {
    background: #2d3643;
    color: #e4e8ee;
    border: none;
    border-right: 1px solid #3a4554;
    border-bottom: 1px solid #3a4554;
    padding: 4px 6px;
}
QTableCornerButton::section {
    background: #2d3643;
    border: none;
}
QStatusBar {
    background: #1e252f;
    border-top: 1px solid #3a4554;
}
QStatusBar::item {
    border: none;
}
QStatusBar QLabel {
    color: #c8d0da;
    padding: 0 6px;
}
QProgressBar {
    background: #2d3643;
    border: 1px solid #3a4554;
    border-radius: 2px;
    text-align: center;
    color: #e4e8ee;
    min-height: 14px;
    max-height: 16px;
}
QProgressBar::chunk {
    background: #4caf50;
    border-radius: 1px;
}
QScrollBar:vertical {
    background: #1e252f;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #4a5568;
    min-height: 24px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background: #5a6b82;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
QScrollBar:horizontal {
    background: #1e252f;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #4a5568;
    min-width: 24px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover {
    background: #5a6b82;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
}
QLineEdit, QSpinBox, QComboBox {
    background: #1e252f;
    border: 1px solid #3a4554;
    border-radius: 3px;
    padding: 4px 6px;
    color: #e4e8ee;
    selection-background-color: #386ea8;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border: 1px solid #4a90d9;
}
/* Spin boxes: the step buttons sit inside the field's frame, in the space the
   right padding reserves, so the frame (and its blue focus outline) stays
   whole around both the value and the buttons, and the value text never runs
   under them. */
QSpinBox {
    padding-right: 26px;
    min-height: 20px;
}
QSpinBox::up-button, QSpinBox::down-button {
    subcontrol-origin: padding;
    width: 22px;
    border: none;
    border-left: 1px solid #3a4554;
    background: #2a3340;
}
QSpinBox::up-button {
    subcontrol-position: top right;
    border-bottom: 1px solid #3a4554;
    border-top-right-radius: 2px;
}
QSpinBox::down-button {
    subcontrol-position: bottom right;
    border-bottom-right-radius: 2px;
}
QSpinBox:focus {
    background: #19202a;
}
QSpinBox::up-button:hover, QSpinBox::down-button:hover {
    background: #3a4659;
}
QSpinBox::up-button:pressed, QSpinBox::down-button:pressed {
    background: #386ea8;
}
QSpinBox::up-arrow {
    image: url(:/icons/chevron-up.png);
    width: 10px;
    height: 10px;
}
QSpinBox::down-arrow {
    image: url(:/icons/chevron-down.png);
    width: 10px;
    height: 10px;
}
QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off,
QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off {
    image: none;
}
QSpinBox::up-button:disabled, QSpinBox::up-button:off,
QSpinBox::down-button:disabled, QSpinBox::down-button:off {
    background: #232a35;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 22px;
    border: none;
}
QComboBox::down-arrow {
    image: url(:/icons/chevron-down.png);
    width: 12px;
    height: 12px;
}
QComboBox QAbstractItemView {
    background: #2d3643;
    border: 1px solid #3a4554;
    selection-background-color: #386ea8;
    color: #e4e8ee;
}
QPushButton {
    background: #2d3643;
    border: 1px solid #3a4554;
    border-radius: 3px;
    padding: 5px 16px;
    color: #e4e8ee;
}
QPushButton:hover {
    background: #384556;
}
QPushButton:pressed {
    background: #386ea8;
}
QPushButton:default {
    border: 1px solid #4a90d9;
}
QCheckBox {
    color: #e4e8ee;
    spacing: 8px;
}
/* Check boxes: a light outline on the dark background and a solid accent
   fill with a white tick once checked, so the state reads at a glance. */
QCheckBox::indicator, QGroupBox::indicator {
    /* 11px + 1px border on each side = 13px, Fusion's original size, so the
       indicator still fits inside group box titles. */
    width: 11px;
    height: 11px;
    border: 1px solid #8a97aa;
    border-radius: 3px;
    background: #1a2029;
}
QCheckBox::indicator:hover, QGroupBox::indicator:hover {
    border-color: #5ea3ec;
    background: #222b37;
}
QCheckBox::indicator:checked, QGroupBox::indicator:checked {
    border-color: #3d8de0;
    background: #3d8de0;
    image: url(:/icons/check.png);
}
QCheckBox::indicator:checked:hover, QGroupBox::indicator:checked:hover {
    border-color: #5ea3ec;
    background: #5ea3ec;
}
QCheckBox::indicator:indeterminate {
    border-color: #3d8de0;
    background: #2a5f99;
}
QCheckBox::indicator:disabled, QGroupBox::indicator:disabled {
    border-color: #4a5464;
    background: #232a35;
}
QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled {
    background: #4a5464;
}
QCheckBox:focus {
    outline: none;
}
QGroupBox {
    border: 1px solid #3a4554;
    border-radius: 4px;
    margin-top: 10px;
    padding-top: 8px;
    color: #e4e8ee;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #96a0ae;
}
QToolTip {
    background: #2d3643;
    color: #e4e8ee;
    border: 1px solid #4a5568;
    padding: 3px;
}
)QSS";

QPalette DarkPalette() {
    QPalette palette;
    const QColor window(30, 37, 47);
    const QColor panel(45, 54, 67);
    const QColor text(228, 232, 238);
    const QColor dim(150, 160, 174);
    const QColor accent(56, 110, 168);

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, window);
    palette.setColor(QPalette::AlternateBase, panel);
    palette.setColor(QPalette::ToolTipBase, panel);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, panel);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, QColor(74, 144, 226));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, dim);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(108, 118, 132));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(108, 118, 132));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(108, 118, 132));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, panel);
    return palette;
}

}  // namespace

void applyDarkTitleBar(QWidget* window) {
    if (window) ApplyDarkTitleBar((HWND)window->winId());
}

void applyApplicationTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(DarkPalette());
    app.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

QIcon applicationIcon() {
    QIcon icon(QStringLiteral(":/app.ico"));
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        icon = IconStartQueue();
    }
    return icon;
}

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------
namespace {
constexpr int ColFile = 0;
constexpr int ColQueue = 1;
constexpr int ColSize = 2;
constexpr int ColStatus = 3;
constexpr int ColTimeLeft = 4;
constexpr int ColRate = 5;
constexpr int ColLastTry = 6;
constexpr int ColDescription = 7;
constexpr int ColCount = 8;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(AppTitle());
    setWindowIcon(applicationIcon());
    setAcceptDrops(true);
    setMinimumSize(860, 500);
    resize(1100, 660);

    buildUi();
    setupTray();

    bridge_ = std::make_unique<ChromeBridge>(this);
    bridge_->setDownloadHandler([this](ChromeDownloadRequest request) {
        enqueueChromeDownload(std::move(request));
    });
    QString bridgeError;
    if (!bridge_->start(48765, 5, &bridgeError)) {
        statusBar()->showMessage(
            Tr("Chrome bridge could not start: %1").arg(bridgeError), 10000);
    }

    auto* timer = new QTimer(this);
    timer->setInterval(300);
    connect(timer, &QTimer::timeout, this, [this] { refresh(false); });
    timer->start();

    refresh(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    buildActions();
    buildMenus();
    buildToolbar();
    buildCategories();
    buildDownloadTable();
    buildStatusBar();

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(false);
    splitter_->addWidget(categoriesPanel_);
    splitter_->addWidget(table_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({ 190, 900 });
    setCentralWidget(splitter_);
}

void MainWindow::buildActions() {
    actAdd_ = new QAction(IconAddUrl(), QStringLiteral("Add URL..."), this);
    actAdd_->setIconText(QStringLiteral("Add URL"));
    actAdd_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(actAdd_, &QAction::triggered, this, [this] { showAddDialog(); });

    actAddClipboard_ = new QAction(QStringLiteral("Add from clipboard"), this);
    actAddClipboard_->setShortcut(QKeySequence(QStringLiteral("Ctrl+V")));
    connect(actAddClipboard_, &QAction::triggered, this,
            [this] { addFromClipboard(); });

    actResume_ = new QAction(IconResume(), QStringLiteral("Download Now"), this);
    actResume_->setIconText(QStringLiteral("Resume"));
    actResume_->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(actResume_, &QAction::triggered, this, [this] { startSelected(); });

    actStop_ = new QAction(IconStop(), QStringLiteral("Stop Download"), this);
    actStop_->setIconText(QStringLiteral("Stop"));
    actStop_->setShortcut(QKeySequence(QStringLiteral("F9")));
    connect(actStop_, &QAction::triggered, this, [this] { stopSelected(); });

    actRedownload_ = new QAction(IconRedownload(),
                                 QStringLiteral("Redownload"), this);
    connect(actRedownload_, &QAction::triggered, this,
            [this] { redownloadSelected(); });

    actStartAll_ = new QAction(QStringLiteral("Start all"), this);
    connect(actStartAll_, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StartAll();
        refresh(false);
    });

    actStopAll_ = new QAction(IconHand(), QStringLiteral("Stop All"), this);
    connect(actStopAll_, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StopAll();
        refresh(false);
    });

    actPauseAll_ = new QAction(IconPause(), QStringLiteral("Pause All"), this);
    connect(actPauseAll_, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StopAll();
        refresh(false);
    });

    actStartQueue_ = new QAction(IconStartQueue(), QStringLiteral("Start Queue"),
                                 this);
    connect(actStartQueue_, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StartAll();
        refresh(false);
    });

    actStopQueue_ = new QAction(IconStopQueue(), QStringLiteral("Stop Queue"),
                                this);
    connect(actStopQueue_, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StopAll();
        refresh(false);
    });

    actDelete_ = new QAction(IconDelete(), QStringLiteral("Remove"), this);
    actDelete_->setIconText(QStringLiteral("Delete"));
    actDelete_->setShortcut(QKeySequence::Delete);
    connect(actDelete_, &QAction::triggered, this,
            [this] { removeSelected(false); });

    actDeleteFile_ = new QAction(QStringLiteral("Delete with file"), this);
    connect(actDeleteFile_, &QAction::triggered, this,
            [this] { removeSelected(true); });

    actDeleteCompleted_ = new QAction(
        IconDeleteCompleted(),
        QStringLiteral("Delete All Completed"), this);
    actDeleteCompleted_->setIconText(QStringLiteral("Delete Completed"));
    connect(actDeleteCompleted_, &QAction::triggered, this,
            [this] { removeCompleted(); });

    actOpen_ = new QAction(QStringLiteral("Open file"), this);
    connect(actOpen_, &QAction::triggered, this,
            [this] { openSelectedFile(); });

    actOpenFolder_ = new QAction(QStringLiteral("Open folder"), this);
    connect(actOpenFolder_, &QAction::triggered, this,
            [this] { openSelectedFolder(); });

    actCopyUrl_ = new QAction(QStringLiteral("Copy URL"), this);
    connect(actCopyUrl_, &QAction::triggered, this,
            [this] { copySelectedUrl(); });

    actSelectAll_ = new QAction(QStringLiteral("Select all"), this);
    actSelectAll_->setShortcut(QKeySequence::SelectAll);
    connect(actSelectAll_, &QAction::triggered, this,
            [this] { table_->selectAll(); });

    actFind_ = new QAction(QStringLiteral("Find"), this);
    actFind_->setShortcut(QKeySequence::Find);
    connect(actFind_, &QAction::triggered, this, [this] { showFindDialog(); });

    actFindNext_ = new QAction(QStringLiteral("Find Next"), this);
    actFindNext_->setShortcut(QKeySequence(QStringLiteral("F3")));
    connect(actFindNext_, &QAction::triggered, this,
            [this] { findNextMatch(); });

    actOptions_ = new QAction(IconOptions(), QStringLiteral("Options"), this);
    actOptions_->setIconText(QStringLiteral("Options"));
    connect(actOptions_, &QAction::triggered, this, [this] { showOptions(); });

    actScheduler_ = new QAction(IconScheduler(),
                               QStringLiteral("Scheduler"), this);
    actScheduler_->setIconText(QStringLiteral("Scheduler"));
    connect(actScheduler_, &QAction::triggered, this, [this] {
        ShowInfo(this, AppTitle(),
                 Tr("The scheduler is not implemented in this personal "
                    "build. Use Start queue / Stop queue instead."));
    });

    actTellFriend_ = new QAction(IconTellFriend(),
                                 QStringLiteral("Tell a friend"), this);
    actTellFriend_->setIconText(QStringLiteral("Tell a Friend"));
    connect(actTellFriend_, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral(
                "mailto:?subject=FLEX%20Download%20Manager")));
    });

    actRefresh_ = new QAction(QStringLiteral("Refresh"), this);
    actRefresh_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(actRefresh_, &QAction::triggered, this, [this] { refresh(true); });

    actViewToolbar_ = new QAction(QStringLiteral("Toolbar"), this);
    actViewToolbar_->setCheckable(true);
    actViewToolbar_->setChecked(true);
    connect(actViewToolbar_, &QAction::toggled, this, [this](bool visible) {
        if (toolbar_) toolbar_->setVisible(visible);
    });

    actViewStatus_ = new QAction(QStringLiteral("Status bar"), this);
    actViewStatus_->setCheckable(true);
    actViewStatus_->setChecked(true);
    connect(actViewStatus_, &QAction::toggled, this,
            [this](bool visible) { statusBar()->setVisible(visible); });

    actViewCategories_ = new QAction(QStringLiteral("Categories"), this);
    actViewCategories_->setCheckable(true);
    actViewCategories_->setChecked(true);
    connect(actViewCategories_, &QAction::toggled, this,
            [this](bool visible) { toggleCategories(visible); });

    actHelp_ = new QAction(QStringLiteral("Help..."), this);
    actHelp_->setShortcut(QKeySequence::HelpContents);
    connect(actHelp_, &QAction::triggered, this, [this] { showHelp(); });

    actAbout_ = new QAction(QStringLiteral("About..."), this);
    connect(actAbout_, &QAction::triggered, this,
            [this] { ShowAboutDialogQt(this); });

    actRegister_ = new QAction(QStringLiteral("Register..."), this);
    connect(actRegister_, &QAction::triggered, this, [this] {
        ShowInfo(this, AppTitle(),
                 Tr("We are living in the year 2077, do you think you still need to use paid software?"));
    });

    actRegistrationInfo_ = new QAction(QStringLiteral("Registration info..."),
                                       this);
    connect(actRegistrationInfo_, &QAction::triggered, this, [this] {
        ShowInfo(this, AppTitle(),
                 Tr("This software is free and open source. If you obtained it from any place, website, entity, or person for a fee, or paid to use certain features in the software, this is a violation of the software's usage policy and you should request a refund immediately."));
    });

    actExit_ = new QAction(QStringLiteral("Exit"), this);
    actExit_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(actExit_, &QAction::triggered, this, [this] { requestQuit(); });

    const struct {
        QAction* action;
        const char* text;
        const char* iconText;
    } translations[] = {
        { actAdd_, "Add URL...", "Add URL" },
        { actAddClipboard_, "Add from clipboard", nullptr },
        { actResume_, "Download Now", "Resume" },
        { actStop_, "Stop Download", "Stop" },
        { actRedownload_, "Redownload", nullptr },
        { actStartAll_, "Start all", nullptr },
        { actStopAll_, "Stop All", nullptr },
        { actPauseAll_, "Pause All", nullptr },
        { actStartQueue_, "Start Queue", "Start Queue" },
        { actStopQueue_, "Stop Queue", "Stop Queue" },
        { actDelete_, "Remove", "Delete" },
        { actDeleteFile_, "Delete with file", nullptr },
        { actDeleteCompleted_, "Delete All Completed", "Delete Completed" },
        { actOpen_, "Open file", nullptr },
        { actOpenFolder_, "Open folder", nullptr },
        { actCopyUrl_, "Copy URL", nullptr },
        { actSelectAll_, "Select all", nullptr },
        { actFind_, "Find", nullptr },
        { actFindNext_, "Find Next", nullptr },
        { actOptions_, "Options", "Options" },
        { actScheduler_, "Scheduler", "Scheduler" },
        { actTellFriend_, "Tell a friend", "Tell a Friend" },
        { actRefresh_, "Refresh", nullptr },
        { actViewToolbar_, "Toolbar", nullptr },
        { actViewStatus_, "Status bar", nullptr },
        { actViewCategories_, "Categories", nullptr },
        { actHelp_, "Help...", nullptr },
        { actAbout_, "About...", nullptr },
        { actRegister_, "Register...", nullptr },
        { actRegistrationInfo_, "Registration info...", nullptr },
        { actExit_, "Exit", nullptr },
    };
    for (const auto& entry : translations) {
        SetActionSource(entry.action, entry.text, entry.iconText);
    }

    // Actions that only live in a menu are not attached to a widget in the
    // main window, so Qt would not route their shortcuts. Register them here
    // (without adding any visible UI).
    for (QAction* action : { actAddClipboard_, actSelectAll_, actFind_,
                             actFindNext_, actRefresh_, actHelp_, actExit_ }) {
        addAction(action);
    }
}

void MainWindow::buildMenus() {
    // Keep the Qt-drawn menu bar (the native Windows one ignores the dark
    // stylesheet) and pin its colours explicitly.
    menuBar()->setNativeMenuBar(false);
    menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar { background-color:#171c24; color:#e4e8ee; padding:2px 4px; "
        "border:none; }"
        "QMenuBar::item { background:transparent; color:#e4e8ee; "
        "padding:5px 10px; }"
        "QMenuBar::item:selected { background:#384556; }"
        "QMenuBar::item:pressed { background:#386ea8; }"));

    auto addMenu = [](QMenu* parent, const char* text) {
        QMenu* menu = parent->addMenu(Tr(text));
        menu->menuAction()->setProperty("enText", QString::fromUtf8(text));
        return menu;
    };
    auto addBarMenu = [](QMenuBar* bar, const char* text) {
        QMenu* menu = bar->addMenu(Tr(text));
        menu->menuAction()->setProperty("enText", QString::fromUtf8(text));
        return menu;
    };
    auto addItem = [](QMenu* menu, const char* text) {
        QAction* action = menu->addAction(Tr(text));
        action->setProperty("enText", QString::fromUtf8(text));
        return action;
    };

    QMenu* tasks = addBarMenu(menuBar(), "&Tasks");
    QAction* addNew = addItem(tasks, "Add new download...");
    connect(addNew, &QAction::triggered, this, [this] { showAddDialog(); });
    tasks->addAction(actAddClipboard_);
    tasks->addSeparator();
    tasks->addAction(actStartQueue_);
    tasks->addAction(actStopQueue_);
    tasks->addAction(actScheduler_);
    tasks->addSeparator();
    tasks->addAction(actExit_);

    QMenu* file = addBarMenu(menuBar(), "&File");
    file->addAction(actStop_);
    file->addAction(actDelete_);
    file->addAction(actResume_);
    file->addAction(actRedownload_);

    QMenu* downloads = addBarMenu(menuBar(), "&Downloads");
    downloads->addAction(actPauseAll_);
    downloads->addAction(actStopAll_);
    downloads->addSeparator();
    downloads->addAction(actDeleteCompleted_);
    downloads->addSeparator();
    downloads->addAction(actFind_);
    downloads->addAction(actFindNext_);
    downloads->addSeparator();
    downloads->addAction(actScheduler_);

    QMenu* startQueue = addMenu(downloads, "Start queue");
    QAction* startMain = addItem(startQueue, "Main queue");
    connect(startMain, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StartAll();
        refresh(false);
    });

    QMenu* stopQueue = addMenu(downloads, "Stop queue");
    QAction* stopMain = addItem(stopQueue, "Main queue");
    connect(stopMain, &QAction::triggered, this, [this] {
        DownloadManager::Instance().StopAll();
        refresh(false);
    });

    downloads->addSeparator();

    speedMenu_ = addMenu(downloads, "Speed Limiter");
    actSpeedOff_ = addItem(speedMenu_, "Turn off");
    actSpeedOff_->setCheckable(true);
    actSpeedOff_->setData(0);
    connect(actSpeedOff_, &QAction::triggered, this,
            [this] { setSpeedLimit(0); });

    speedMenu_->addSeparator();
    for (int kb : { 100, 250, 500, 1000, 2000 }) {
        const QString label = QStringLiteral("%1 KB/s").arg(kb);
        QAction* preset = speedMenu_->addAction(Tr(label));
        preset->setProperty("enText", label);
        preset->setCheckable(true);
        preset->setData(kb);
        connect(preset, &QAction::triggered, this,
                [this, kb] { setSpeedLimit(kb); });
    }
    speedMenu_->addSeparator();
    QAction* customSpeed = addItem(speedMenu_, "Custom...");
    connect(customSpeed, &QAction::triggered, this, [this] {
        DownloadManager& manager = DownloadManager::Instance();
        const int current = std::max(0, manager.Settings().speedLimitKB);

        QInputDialog dialog(this);
        dialog.setWindowTitle(AppTitle());
        dialog.setInputMode(QInputDialog::IntInput);
        dialog.setLabelText(Tr("Speed limit (0 = unlimited):"));
        dialog.setIntRange(0, 1000000);
        dialog.setIntValue(current);
        dialog.setIntStep(50);
        dialog.setOkButtonText(Tr("OK"));
        dialog.setCancelButtonText(Tr("Cancel"));
        if (dialog.exec() == QDialog::Accepted) {
            setSpeedLimit(dialog.intValue());
        }
    });

    connect(speedMenu_, &QMenu::aboutToShow, this, [this] {
        const int limit =
            std::max(0, DownloadManager::Instance().Settings().speedLimitKB);
        for (QAction* action : speedMenu_->actions()) {
            if (!action->data().isValid()) continue;
            action->setChecked(action->data().toInt() == limit);
        }
    });

    downloads->addSeparator();
    downloads->addAction(actOptions_);

    QMenu* view = addBarMenu(menuBar(), "&View");

    QMenu* arrange = addMenu(view, "Arrange files");
    auto* sortGroup = new QActionGroup(this);
    sortGroup->setExclusive(true);
    const struct {
        const char* text;
        SortMode mode;
    } sortEntries[] = {
        { "By Order Of Addition", SortMode::AdditionOrder },
        { "By File Name", SortMode::FileName },
        { "By Size", SortMode::Size },
        { "By Status", SortMode::Status },
        { "By Time Left", SortMode::TimeLeft },
        { "By Transfer Rate", SortMode::TransferRate },
        { "By Last Try Date", SortMode::LastTryDate },
        { "By Description", SortMode::Description },
        { "By Save Path", SortMode::SavePath },
        { "By Referer", SortMode::Referer },
        { "By Parent Web Page", SortMode::ParentPage },
    };
    for (const auto& entry : sortEntries) {
        QAction* action = addItem(arrange, entry.text);
        action->setCheckable(true);
        action->setChecked(sort_ == entry.mode);
        sortGroup->addAction(action);
        const SortMode mode = entry.mode;
        connect(action, &QAction::triggered, this,
                [this, mode] { setSortMode(mode); });
    }

    view->addSeparator();
    view->addAction(actViewToolbar_);
    view->addAction(actViewStatus_);
    view->addAction(actViewCategories_);
    view->addSeparator();

    QMenu* language = addMenu(view, "Language");
    auto* languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);

    actLanguageEnglish_ = language->addAction(QStringLiteral("English"));
    actLanguageEnglish_->setCheckable(true);
    actLanguageEnglish_->setChecked(CurrentLanguage() == UiLanguage::English);
    languageGroup->addAction(actLanguageEnglish_);
    connect(actLanguageEnglish_, &QAction::triggered, this,
            [this] { setLanguage(UiLanguage::English); });

    actLanguageArabic_ =
        language->addAction(QString::fromUtf8("العربية"));
    actLanguageArabic_->setCheckable(true);
    actLanguageArabic_->setChecked(CurrentLanguage() == UiLanguage::Arabic);
    languageGroup->addAction(actLanguageArabic_);
    connect(actLanguageArabic_, &QAction::triggered, this,
            [this] { setLanguage(UiLanguage::Arabic); });

    view->addSeparator();
    view->addAction(actRefresh_);

    QMenu* help = addBarMenu(menuBar(), "&Help");
    help->addAction(actHelp_);
    help->addAction(actAbout_);

    QMenu* registration = addBarMenu(menuBar(), "&Registration");
    registration->addAction(actRegister_);
    registration->addAction(actRegistrationInfo_);
}

void MainWindow::buildToolbar() {
    toolbar_ = addToolBar(QStringLiteral("Main"));
    toolbar_->setObjectName(QStringLiteral("mainToolbar"));
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setAllowedAreas(Qt::TopToolBarArea);
    toolbar_->setIconSize(QSize(32, 32));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar_->setContextMenuPolicy(Qt::PreventContextMenu);

    toolbar_->addAction(actAdd_);
    toolbar_->addSeparator();
    toolbar_->addAction(actResume_);
    toolbar_->addAction(actStop_);
    toolbar_->addAction(actStopAll_);
    toolbar_->addAction(actDelete_);
    toolbar_->addAction(actDeleteCompleted_);
    toolbar_->addSeparator();
    toolbar_->addAction(actOptions_);
    toolbar_->addSeparator();
    toolbar_->addAction(actScheduler_);
    toolbar_->addAction(actStartQueue_);
    toolbar_->addAction(actStopQueue_);
    toolbar_->addAction(actTellFriend_);
}

void MainWindow::buildCategories() {
    categoriesPanel_ = new QWidget(this);
    categoriesPanel_->setObjectName(QStringLiteral("categoriesPanel"));
    categoriesPanel_->setMinimumWidth(130);

    auto* layout = new QVBoxLayout(categoriesPanel_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(categoriesPanel_);
    header->setObjectName(QStringLiteral("categoriesHeader"));
    header->setFixedHeight(26);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 0, 3, 0);
    headerLayout->setSpacing(0);

    auto* title = new QLabel(Tr("Categories"), header);
    title->setObjectName(QStringLiteral("categoriesTitle"));
    title->setProperty("enText", QStringLiteral("Categories"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);

    auto* closeButton = new QToolButton(header);
    closeButton->setObjectName(QStringLiteral("categoriesClose"));
    closeButton->setText(QStringLiteral("\u2715"));
    closeButton->setAutoRaise(true);
    closeButton->setCursor(Qt::ArrowCursor);
    closeButton->setToolTip(Tr("Hide categories"));
    connect(closeButton, &QToolButton::clicked, this, [this] {
        actViewCategories_->setChecked(false);
    });
    headerLayout->addWidget(closeButton);

    layout->addWidget(header);

    categories_ = new QTreeWidget(categoriesPanel_);
    categories_->setObjectName(QStringLiteral("categoriesTree"));
    categories_->setHeaderHidden(true);
    categories_->setFrameShape(QFrame::NoFrame);
    categories_->setIndentation(15);
    categories_->setRootIsDecorated(true);
    categories_->setUniformRowHeights(true);
    categories_->setAnimated(false);
    layout->addWidget(categories_, 1);

    auto* all = new QTreeWidgetItem(categories_,
                                    QStringList{ Tr("All Downloads") });
    all->setData(0, Qt::UserRole, QStringLiteral("all"));
    all->setData(0, Qt::UserRole + 1, QStringLiteral("All Downloads"));
    all->setIcon(0, FolderIcon());

    auto addChild = [all](const QString& text, const QString& key,
                          const QIcon& icon) {
        auto* child = new QTreeWidgetItem(all, QStringList{ Tr(text) });
        child->setData(0, Qt::UserRole, key);
        child->setData(0, Qt::UserRole + 1, text);
        child->setIcon(0, icon);
    };
    addChild(QStringLiteral("Compressed"), QStringLiteral("compressed"),
             FileTypeIcon(QStringLiteral("archive.zip")));
    addChild(QStringLiteral("Documents"), QStringLiteral("documents"),
             FileTypeIcon(QStringLiteral("document.pdf")));
    addChild(QStringLiteral("Music"), QStringLiteral("music"),
             FileTypeIcon(QStringLiteral("track.mp3")));
    addChild(QStringLiteral("Programs"), QStringLiteral("programs"),
             FileTypeIcon(QStringLiteral("setup.exe")));
    addChild(QStringLiteral("Video"), QStringLiteral("video"),
             FileTypeIcon(QStringLiteral("movie.mp4")));

    auto addTop = [this](const QString& text, const QString& key) {
        auto* entry = new QTreeWidgetItem(categories_, QStringList{ Tr(text) });
        entry->setData(0, Qt::UserRole, key);
        entry->setData(0, Qt::UserRole + 1, text);
        entry->setIcon(0, FolderIcon());
        return entry;
    };
    addTop(QStringLiteral("Unfinished"), QStringLiteral("unfinished"));
    addTop(QStringLiteral("Finished"), QStringLiteral("finished"));
    addTop(QStringLiteral("Grabber projects"), QStringLiteral("grabber"));
    addTop(QStringLiteral("Queues"), QStringLiteral("queues"));

    categories_->expandItem(all);
    categories_->setCurrentItem(all);
    connect(categories_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current) return;
                category_ = current->data(0, Qt::UserRole).toString();
                refresh(true);
            });
}

void MainWindow::buildDownloadTable() {
    table_ = new QTableWidget(0, ColCount, this);
    table_->setObjectName(QStringLiteral("downloadTable"));
    table_->setHorizontalHeaderLabels(QStringList{
        Tr("File Name"), Tr("Q"), Tr("Size"), Tr("Status"),
        Tr("Time left"), Tr("Transfer rate"), Tr("Last Try Date"),
        Tr("Description") });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(true);
    table_->setWordWrap(false);
    table_->setSortingEnabled(false);
    table_->setAlternatingRowColors(false);
    table_->setIconSize(QSize(16, 16));
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(23);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setStretchLastSection(false);

    // The file name and the description share the slack; the remaining
    // columns keep a fixed width so no header scrolls out of view.
    const int widths[ColCount] = { 300, 28, 80, 130, 80, 100, 120, 200 };
    for (int i = 0; i < ColCount; ++i) {
        table_->setColumnWidth(i, widths[i]);
    }
    table_->horizontalHeader()->setSectionResizeMode(ColFile,
                                                     QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColDescription,
                                                     QHeaderView::Stretch);
    for (int i = ColQueue; i <= ColLastTry; ++i) {
        table_->horizontalHeader()->setSectionResizeMode(i,
                                                         QHeaderView::Interactive);
    }

    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        const auto items = DownloadManager::Instance().Snapshot();
        updateActions(items);
        updateStatus(items);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { openSelectedFile(); });

    // The Downloads menu mirrors IDM's layout, so the per-item commands live
    // in a context menu (as they do in IDM) instead of the main menu.
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                QTableWidgetItem* clicked = table_->itemAt(pos);
                if (!clicked) return;
                if (table_->selectionModel()->selectedRows().isEmpty() ||
                    clicked->row() != table_->currentRow()) {
                    table_->setCurrentCell(clicked->row(), ColFile);
                    table_->selectRow(clicked->row());
                }

                updateActions(DownloadManager::Instance().Snapshot());

                QMenu menu(this);
                menu.addAction(actResume_);
                menu.addAction(actStop_);
                menu.addAction(actRedownload_);
                menu.addSeparator();
                menu.addAction(actDelete_);
                menu.addAction(actDeleteFile_);
                menu.addSeparator();
                menu.addAction(actOpen_);
                menu.addAction(actOpenFolder_);
                menu.addAction(actCopyUrl_);
                menu.addSeparator();
                menu.addAction(actSelectAll_);
                menu.exec(table_->viewport()->mapToGlobal(pos));
            });
}

void MainWindow::buildStatusBar() {
    statusLabel_ = new QLabel(statusBar());
    statusLabel_->setObjectName(QStringLiteral("statusMessage"));
    speedLabel_ = new QLabel(statusBar());
    progress_ = new QProgressBar(statusBar());
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(true);
    progress_->setFixedWidth(160);
    progress_->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#2d3643; border:1px solid #3a4554; "
        "border-radius:2px; text-align:center; color:#e4e8ee; }"
        "QProgressBar::chunk { background:#4caf50; }"));

    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(speedLabel_);
    statusBar()->addPermanentWidget(progress_);
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(applicationIcon(), this);
    tray_->setToolTip(AppTitle());

    auto* menu = new QMenu(this);
    QAction* showAction = menu->addAction(Tr("Show main window"), this,
                                          [this] { restoreFromTray(); });
    showAction->setProperty("enText", QStringLiteral("Show main window"));
    menu->addSeparator();
    QAction* exitAction =
        menu->addAction(Tr("Exit"), this, [this] { requestQuit(); });
    exitAction->setProperty("enText", QStringLiteral("Exit"));
    tray_->setContextMenu(menu);

    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick ||
                    reason == QSystemTrayIcon::Trigger) {
                    restoreFromTray();
                }
            });
    tray_->show();
}

void MainWindow::hideToTray() {
    if (closing_) return;
    if (!tray_ || !tray_->isVisible()) return;
    hide();
}

void MainWindow::requestQuit() {
    quitRequested_ = true;
    close();
}

void MainWindow::restoreFromTray() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::enqueueChromeDownload(ChromeDownloadRequest request) {
    if (closing_) return;
    chromeQueue_.push_back(std::move(request));
    if (!chromeDialogOpen_) {
        QTimer::singleShot(0, this, [this] { showNextChromeDownload(); });
    }
}

void MainWindow::showNextChromeDownload() {
    if (closing_ || chromeDialogOpen_ || chromeQueue_.empty()) return;

    chromeDialogOpen_ = true;
    ChromeDownloadRequest request = std::move(chromeQueue_.front());
    chromeQueue_.pop_front();

    DownloadManager& manager = DownloadManager::Instance();
    Config& cfg = manager.MutableSettings();

    DownloadInfoResult result;
    result.url = request.url;
    result.fileName = request.fileName;
    result.description = request.description.empty() ? request.referrer
                                                     : request.description;
    result.totalBytes = request.totalBytes;
    result.categoryKey = request.category;
    if (result.categoryKey.empty()) {
        result.categoryKey = InferCategory(result.fileName, request.mime);
    }
    result.connections = cfg.maxConnections;
    result.startNow = true;

    const bool accepted = ShowDownloadFileInfoQt(nullptr, cfg, result, true);
    if (accepted) {
        bool settingsChanged = false;
        if (result.remember && !result.rememberPath.empty()) {
            cfg.categoryPaths[result.categoryKey] = result.rememberPath;
            settingsChanged = true;
        }
        if (result.categoryKey.rfind(L"custom:", 0) == 0) {
            const std::wstring name = result.categoryKey.substr(7);
            if (!name.empty() &&
                std::find(cfg.customCategories.begin(),
                          cfg.customCategories.end(),
                          name) == cfg.customCategories.end()) {
                cfg.customCategories.push_back(name);
                settingsChanged = true;
            }
        }
        if (!result.recentSavePaths.empty() &&
            cfg.recentSavePaths != result.recentSavePaths) {
            cfg.recentSavePaths = result.recentSavePaths;
            settingsChanged = true;
        }
        if (settingsChanged) manager.SaveSettings();

        const std::wstring directory = DirName(result.savePath);
        const std::wstring fileName = FileNameFromPath(result.savePath);
        auto item = request.formatId.empty()
                        ? manager.Add(result.url, directory, fileName,
                                      result.connections, result.startNow,
                                      result.description, result.categoryKey)
                        : manager.AddYtDlp(result.url, directory, fileName,
                                           request.formatId, request.merge,
                                           result.startNow,
                                           result.description,
                                           result.categoryKey);
        if (!item) {
            ShowCritical(this, AppTitle(),
                         Tr("Could not create the download."));
        } else {
            refresh(true);
            selectId(item->id);
            if (cfg.showProgressDialog) {
                ShowDownloadProgressQt(this, item);
            }
        }
    }

    chromeDialogOpen_ = false;
    if (!chromeQueue_.empty()) {
        QTimer::singleShot(0, this, [this] { showNextChromeDownload(); });
    }
}

void MainWindow::detectCompletedDownloads(
    const std::vector<std::shared_ptr<DownloadItem>>& items) {
    // The first pass records anything that is already complete (for example a
    // list restored in the future) so old entries do not pop the dialog at
    // startup.
    if (!completionBaselineDone_) {
        for (const auto& item : items) {
            if (item->GetState() == DownloadState::Complete) {
                announcedComplete_.insert(item->id);
            }
        }
        completionBaselineDone_ = true;
        return;
    }

    bool queued = false;
    for (const auto& item : items) {
        if (item->GetState() != DownloadState::Complete) {
            // A re-download leaves Complete, so announce the next finish again.
            announcedComplete_.erase(item->id);
            continue;
        }
        if (!announcedComplete_.insert(item->id).second) continue;
        if (closing_) continue;
        if (!DownloadManager::Instance().Settings().showCompleteDialog) continue;
        completeQueue_.push_back(item);
        queued = true;
    }

    if (queued && !completeDialogOpen_) {
        QTimer::singleShot(0, this, [this] { showNextDownloadComplete(); });
    }
}

void MainWindow::showNextDownloadComplete() {
    if (completeDialogOpen_ || closing_) return;
    completeDialogOpen_ = true;

    DownloadManager& manager = DownloadManager::Instance();
    while (!completeQueue_.empty() && !closing_) {
        const auto item = completeQueue_.front();
        completeQueue_.pop_front();

        Config& cfg = manager.MutableSettings();
        if (!cfg.showCompleteDialog) continue;

        // Close the progress window first: otherwise it sits behind the
        // completion window for the remainder of its 900 ms grace period.
        CloseDownloadProgressQt(item->id);

        const bool dontShow = ShowDownloadCompleteQt(
            isVisible() && !isMinimized() ? this : nullptr, item->url,
            item->GetSavePath(), item->downloadedBytes.load());
        if (dontShow) {
            cfg.showCompleteDialog = false;
            manager.SaveSettings();
        }
    }

    completeDialogOpen_ = false;
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    applyDarkTitleBar(this);
    // Tag the real top-level window. A second launch uses this property to
    // find it; searching by title can hit one of Qt's hidden menu-shadow
    // helpers that share the application title.
    SetPropW(reinterpret_cast<HWND>(winId()), FlexMainWindowProperty(),
             reinterpret_cast<HANDLE>(1));
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                             qintptr* result) {
    // A second launch (desktop icon, Start menu) posts this message so the
    // window is restored by Qt itself rather than by ShowWindow() from the
    // other process, which would leave an unpainted white frame behind.
    if (eventType == QByteArrayLiteral("windows_generic_MSG") ||
        eventType == QByteArrayLiteral("windows_dispatcher_MSG")) {
        MSG* msg = static_cast<MSG*>(message);
        if (msg && msg->message == FlexShowMainWindowMessage()) {
            if (result) *result = 0;
            restoreFromTray();
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (path.endsWith(QStringLiteral(".url"), Qt::CaseInsensitive)) {
                wchar_t buffer[4096] = {};
                GetPrivateProfileStringW(
                    L"InternetShortcut", L"URL", L"", buffer, 4096,
                    (const wchar_t*)path.utf16());
                if (buffer[0]) addUrlFromText(buffer, true);
            } else if (ParseUrl(ToW(path)).valid) {
                addUrlFromText(ToW(path), true);
            }
        } else {
            const QString text = url.toString();
            if (ParseUrl(ToW(text)).valid) addUrlFromText(ToW(text), true);
        }
    }
    event->acceptProposedAction();
}

// ---------------------------------------------------------------------------
// Filtering and list refresh
// ---------------------------------------------------------------------------
bool MainWindow::matchesCategory(DownloadItem& item) const {
    if (category_ == QStringLiteral("all")) return true;

    const DownloadState state = item.GetState();
    if (category_ == QStringLiteral("unfinished")) {
        return state != DownloadState::Complete;
    }
    if (category_ == QStringLiteral("finished")) {
        return state == DownloadState::Complete;
    }
    if (category_ == QStringLiteral("grabber") ||
        category_ == QStringLiteral("queues")) {
        return false;
    }

    if (!item.category.empty()) {
        return ToQ(item.category) == category_;
    }

    const QString extension =
        QFileInfo(ToQ(item.GetFileName())).suffix().toLower();
    static const QStringList archives = { "zip", "rar", "7z", "tar", "gz",
                                          "bz2", "xz", "zst" };
    static const QStringList documents = { "pdf", "doc", "docx", "xls", "xlsx",
                                           "ppt", "pptx", "txt", "rtf", "csv",
                                           "epub", "md" };
    static const QStringList music = { "mp3", "wav", "flac", "aac", "ogg",
                                       "m4a", "wma" };
    static const QStringList programs = { "exe", "msi", "apk", "jar", "deb",
                                          "rpm" };
    static const QStringList video = { "mp4", "mkv", "avi", "mov", "wmv",
                                       "flv", "webm", "m4v", "mpg", "mpeg",
                                       "ts" };

    if (category_ == QStringLiteral("compressed")) {
        return archives.contains(extension);
    }
    if (category_ == QStringLiteral("documents")) {
        return documents.contains(extension);
    }
    if (category_ == QStringLiteral("music")) return music.contains(extension);
    if (category_ == QStringLiteral("programs")) {
        return programs.contains(extension);
    }
    if (category_ == QStringLiteral("video")) return video.contains(extension);
    return true;
}

void MainWindow::refresh(bool force) {
    if (closing_) return;

    const auto all = DownloadManager::Instance().Snapshot();
    detectCompletedDownloads(all);

    std::vector<std::shared_ptr<DownloadItem>> visible;
    visible.reserve(all.size());
    for (const auto& item : all) {
        if (matchesCategory(*item)) visible.push_back(item);
    }
    sortVisible(visible);

    std::vector<uint64_t> ids;
    ids.reserve(visible.size());
    for (const auto& item : visible) ids.push_back(item->id);

    if (force || ids != rowIds_) {
        rebuildTable(visible);
    } else {
        for (size_t i = 0; i < visible.size(); ++i) {
            updateTableRow((int)i, visible[i]);
        }
    }

    updateActions(all);
    updateStatus(all);
}

void MainWindow::sortVisible(
    std::vector<std::shared_ptr<DownloadItem>>& items) const {
    if (sort_ == SortMode::AdditionOrder || items.size() < 2) return;

    auto nameOf = [](const std::shared_ptr<DownloadItem>& item) {
        return ToQ(item->GetFileName());
    };
    auto stampOf = [](const std::shared_ptr<DownloadItem>& item) {
        const uint64_t finished = item->finishedAt.load();
        return finished != 0 ? finished : item->startedAt.load();
    };
    auto statusRank = [](DownloadState state) {
        switch (state) {
            case DownloadState::Downloading: return 0;
            case DownloadState::Connecting:  return 1;
            case DownloadState::Queued:      return 2;
            case DownloadState::Paused:      return 3;
            case DownloadState::Error:       return 4;
            case DownloadState::Complete:    return 5;
            case DownloadState::Cancelled:   return 6;
        }
        return 7;
    };
    auto remaining = [](const std::shared_ptr<DownloadItem>& item) {
        if (!item->IsRunning()) return UINT64_MAX;
        const double speed = item->speed.load();
        const uint64_t total = item->totalBytes.load();
        const uint64_t done = item->downloadedBytes.load();
        if (speed <= 0.0 || total == 0) return UINT64_MAX;
        if (done >= total) return (uint64_t)0;
        return (uint64_t)std::ceil((double)(total - done) / speed);
    };
    auto compareText = [](const QString& left, const QString& right) {
        return QString::localeAwareCompare(left.toCaseFolded(),
                                           right.toCaseFolded());
    };

    std::stable_sort(items.begin(), items.end(),
                     [&](const std::shared_ptr<DownloadItem>& a,
                         const std::shared_ptr<DownloadItem>& b) {
        switch (sort_) {
            case SortMode::FileName:
                return compareText(nameOf(a), nameOf(b)) < 0;
            case SortMode::Size:
                return a->totalBytes.load() < b->totalBytes.load();
            case SortMode::Status:
                return statusRank(a->GetState()) < statusRank(b->GetState());
            case SortMode::TimeLeft:
                return remaining(a) < remaining(b);
            case SortMode::TransferRate:
                return a->speed.load() > b->speed.load();
            case SortMode::LastTryDate:
                return stampOf(a) > stampOf(b);
            case SortMode::Description:
                return compareText(a->description.empty() ? ToQ(a->url)
                                                          : ToQ(a->description),
                                   b->description.empty() ? ToQ(b->url)
                                                          : ToQ(b->description)) < 0;
            case SortMode::SavePath:
                return compareText(ToQ(a->GetSavePath()),
                                   ToQ(b->GetSavePath())) < 0;
            case SortMode::Referer:
                // The engine does not retain a Referer header, so the request
                // URL is the closest stable source reference.
                return compareText(ToQ(a->url), ToQ(b->url)) < 0;
            case SortMode::ParentPage: {
                const int host = compareText(ToQ(ParseUrl(a->url).host),
                                             ToQ(ParseUrl(b->url).host));
                if (host != 0) return host < 0;
                return compareText(ToQ(a->url), ToQ(b->url)) < 0;
            }
            case SortMode::AdditionOrder:
                return false;
        }
        return false;
    });
}

void MainWindow::setSortMode(SortMode mode) {
    if (sort_ == mode) return;
    sort_ = mode;
    refresh(true);
}

void MainWindow::setLanguage(UiLanguage language) {
    if (CurrentLanguage() == language) return;

    SetCurrentLanguage(language);
    DownloadManager& manager = DownloadManager::Instance();
    manager.MutableSettings().language =
        language == UiLanguage::Arabic ? 1 : 0;
    manager.SaveSettings();

    retranslateUi();
    refresh(true);

    if (actLanguageEnglish_) {
        actLanguageEnglish_->setChecked(language == UiLanguage::English);
    }
    if (actLanguageArabic_) {
        actLanguageArabic_->setChecked(language == UiLanguage::Arabic);
    }
}

void MainWindow::retranslateUi() {
    for (QAction* action : findChildren<QAction*>()) {
        const QVariant text = action->property("enText");
        if (text.isValid()) action->setText(Tr(text.toString()));
        const QVariant iconText = action->property("enIconText");
        if (iconText.isValid()) action->setIconText(Tr(iconText.toString()));
    }

    for (QLabel* label : findChildren<QLabel*>()) {
        const QVariant text = label->property("enText");
        if (text.isValid()) label->setText(Tr(text.toString()));
    }

    if (auto* close =
            findChild<QToolButton*>(QStringLiteral("categoriesClose"))) {
        close->setToolTip(Tr("Hide categories"));
    }

    if (table_) {
        table_->setHorizontalHeaderLabels(QStringList{
            Tr("File Name"), Tr("Q"), Tr("Size"), Tr("Status"),
            Tr("Time left"), Tr("Transfer rate"), Tr("Last Try Date"),
            Tr("Description") });
    }

    if (categories_) {
        std::function<void(QTreeWidgetItem*)> translate =
            [&](QTreeWidgetItem* item) {
                const QVariant source = item->data(0, Qt::UserRole + 1);
                if (source.isValid()) item->setText(0, Tr(source.toString()));
                for (int i = 0; i < item->childCount(); ++i) {
                    translate(item->child(i));
                }
            };
        for (int i = 0; i < categories_->topLevelItemCount(); ++i) {
            translate(categories_->topLevelItem(i));
        }
    }

    updateStatus(DownloadManager::Instance().Snapshot());
}

void MainWindow::rebuildTable(
    const std::vector<std::shared_ptr<DownloadItem>>& items) {
    const uint64_t keep = selectedId();
    const int scroll = table_->verticalScrollBar()->value();
    findRow_ = -1;

    table_->setUpdatesEnabled(false);
    table_->clearContents();
    table_->setRowCount((int)items.size());
    rowIds_.clear();

    for (size_t i = 0; i < items.size(); ++i) {
        const int row = (int)i;
        const auto& item = items[i];
        rowIds_.push_back(item->id);

        for (int column = 0; column < ColCount; ++column) {
            auto* cell = new QTableWidgetItem();
            cell->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            table_->setItem(row, column, cell);
        }
        table_->item(row, ColFile)->setData(
            Qt::UserRole, QVariant::fromValue<qulonglong>(item->id));

        updateTableRow(row, item);
    }

    table_->setUpdatesEnabled(true);

    if (keep) selectId(keep);
    table_->verticalScrollBar()->setValue(scroll);
}

void MainWindow::updateTableRow(int row,
                                const std::shared_ptr<DownloadItem>& item) {
    if (row < 0 || row >= table_->rowCount()) return;

    auto setText = [this, row](int column, const QString& text) {
        auto* cell = table_->item(row, column);
        if (!cell) return;
        if (cell->text() != text) cell->setText(text);
    };

    const std::wstring fileName = item->GetFileName();
    const QString name = ToQ(fileName);

    auto* nameCell = table_->item(row, ColFile);
    if (nameCell) {
        if (nameCell->text() != name) nameCell->setText(name);
        nameCell->setIcon(FileTypeIcon(name));
        nameCell->setToolTip(name);
    }

    setText(ColQueue, QString());

    const uint64_t total = item->totalBytes.load();
    setText(ColSize, total > 0 ? ToQ(FormatBytes(total)) : QStringLiteral("--"));

    DownloadState state = item->GetState();
    QString status = StateLabel(state);
    if ((state == DownloadState::Downloading || state == DownloadState::Paused) && total > 0) {
        const uint64_t done = item->downloadedBytes.load();
        const int percent = (int)std::min<uint64_t>(100, (done * 100) / total);
        status += QStringLiteral(" %1%").arg(percent);
    }
    if (state == DownloadState::Error) {
        const std::wstring detail = item->GetError();
        if (!detail.empty()) status += QStringLiteral(" - ") + ToQ(detail);
    }
    setText(ColStatus, status);

    setText(ColTimeLeft, TimeLeftText(*item));
    setText(ColRate, item->IsRunning() && item->speed.load() > 0.0
                         ? ToQ(FormatSpeed(item->speed.load()))
                         : QString());
    setText(ColLastTry, LastTryText(*item));
    const QString description =
        item->description.empty() ? ToQ(item->url) : ToQ(item->description);
    setText(ColDescription, description);
    table_->item(row, ColDescription)->setToolTip(description);
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------
uint64_t MainWindow::selectedId() const {
    if (!table_) return 0;
    const int row = table_->currentRow();
    if (row < 0 || row >= (int)rowIds_.size()) return 0;
    if (!table_->selectionModel() || table_->selectionModel()->selectedRows().isEmpty()) {
        return 0;
    }
    return rowIds_[(size_t)row];
}

void MainWindow::selectId(uint64_t id) {
    for (size_t row = 0; row < rowIds_.size(); ++row) {
        if (rowIds_[row] != id) continue;
        const int index = (int)row;
        table_->setCurrentCell(index, ColFile);
        table_->selectRow(index);
        table_->scrollToItem(table_->item(index, ColFile),
                             QAbstractItemView::EnsureVisible);
        return;
    }
}

std::shared_ptr<DownloadItem> MainWindow::selectedItem() const {
    const uint64_t id = selectedId();
    if (!id) return nullptr;
    return DownloadManager::Instance().Find(id);
}

// ---------------------------------------------------------------------------
// Actions and status
// ---------------------------------------------------------------------------
void MainWindow::updateActions(
    const std::vector<std::shared_ptr<DownloadItem>>& items) {
    const auto item = selectedItem();
    const bool has = item != nullptr;
    const bool running = has && item->IsRunning();
    const bool complete = has && item->GetState() == DownloadState::Complete;

    bool anyComplete = false;
    bool anyActive = false;
    for (const auto& entry : items) {
        const DownloadState state = entry->GetState();
        if (state == DownloadState::Complete) anyComplete = true;
        if (state == DownloadState::Connecting ||
            state == DownloadState::Downloading) {
            anyActive = true;
        }
    }

    actResume_->setEnabled(has && !running && !complete);
    actStop_->setEnabled(running);
    actPauseAll_->setEnabled(anyActive);
    actRedownload_->setEnabled(complete);
    actDelete_->setEnabled(has);
    actDeleteFile_->setEnabled(has);
    actOpen_->setEnabled(complete);
    actOpenFolder_->setEnabled(has);
    actCopyUrl_->setEnabled(has);
    actDeleteCompleted_->setEnabled(anyComplete);
    actFind_->setEnabled(!items.empty());
    actFindNext_->setEnabled(!items.empty() && !findText_.isEmpty());
    actStartAll_->setEnabled(!items.empty());
    actStartQueue_->setEnabled(!items.empty());
    actStopAll_->setEnabled(anyActive);
    actStopQueue_->setEnabled(anyActive);
    actRefresh_->setEnabled(true);
}

void MainWindow::updateStatus(
    const std::vector<std::shared_ptr<DownloadItem>>& items) {
    int active = 0;
    int complete = 0;
    int failed = 0;
    double totalSpeed = 0.0;

    for (const auto& item : items) {
        const DownloadState state = item->GetState();
        if (state == DownloadState::Connecting ||
            state == DownloadState::Downloading) {
            ++active;
            totalSpeed += item->speed.load();
        } else if (state == DownloadState::Complete) {
            ++complete;
        } else if (state == DownloadState::Error) {
            ++failed;
        }
    }

    if (items.empty()) {
        statusLabel_->setText(
            Tr("Ready. Use Add URL or paste a link to start downloading."));
    } else {
        statusLabel_->setText(
            Tr("%1 active, %2 completed, %3 failed - %4 total")
                .arg(active)
                .arg(complete)
                .arg(failed)
                .arg((int)items.size()));
    }

    speedLabel_->setText(totalSpeed > 0.0 ? ToQ(FormatSpeed(totalSpeed))
                                          : QString());

    uint64_t total = 0;
    uint64_t done = 0;
    if (const auto selected = selectedItem()) {
        total = selected->totalBytes.load();
        done = selected->downloadedBytes.load();
    } else {
        for (const auto& item : items) {
            total += item->totalBytes.load();
            done += item->downloadedBytes.load();
        }
    }

    int percent = 0;
    if (total > 0) {
        const double value = 100.0 * (double)done / (double)total;
        percent = (int)(value > 100.0 ? 100.0 : value);
    }
    progress_->setValue(percent);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void MainWindow::showAddDialog() {
    DownloadManager& manager = DownloadManager::Instance();
    AddDownloadResult result;
    result.saveDir = manager.Settings().saveDir;
    result.connections = manager.Settings().maxConnections;
    result.startNow = true;

    if (!ShowAddDownloadDialogQt(this, manager.Settings(), result)) return;

    auto item = manager.Add(result.url, result.saveDir, result.fileName,
                            result.connections, result.startNow);
    if (!item) {
        ShowCritical(this, AppTitle(), Tr("Could not create the download."));
        return;
    }
    refresh(true);
    selectId(item->id);
    if (result.startNow && manager.Settings().showProgressDialog) {
        ShowDownloadProgressQt(this, item);
    }
}

void MainWindow::addUrlFromText(const std::wstring& url, bool notify,
                                bool startNow) {
    UrlParts parts = ParseUrl(url);
    if (!parts.valid) {
        if (notify) {
            ShowInfo(this, AppTitle(),
                     Tr("The clipboard does not contain a supported "
                        "http, https or ftp URL."));
        }
        return;
    }

    DownloadManager& manager = DownloadManager::Instance();
    const Config& cfg = manager.Settings();
    const bool starting = startNow || cfg.startOnAdd;
    auto item = manager.Add(url, cfg.saveDir, parts.FileName(),
                            cfg.maxConnections, starting);
    if (!item) {
        ShowCritical(this, AppTitle(), Tr("Could not create the download."));
        return;
    }
    refresh(true);
    selectId(item->id);
    if (starting && cfg.showProgressDialog) {
        ShowDownloadProgressQt(this, item);
    }
}

void MainWindow::addFromClipboard() {
    const QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) {
        ShowInfo(this, AppTitle(), Tr("The clipboard is empty."));
        return;
    }

    const QStringList lines = text.split(QRegularExpression(
        QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString candidate = line.trimmed();
        if (ParseUrl(ToW(candidate)).valid) {
            addUrlFromText(ToW(candidate), true);
            return;
        }
    }
    addUrlFromText(ToW(text), true);
}

void MainWindow::startSelected() {
    const auto item = selectedItem();
    if (!item) return;

    const DownloadState state = item->GetState();
    if (state == DownloadState::Connecting ||
        state == DownloadState::Downloading) {
        return;
    }
    item->Start(DownloadManager::Instance().Settings());
    refresh(false);
}

void MainWindow::stopSelected() {
    if (const auto item = selectedItem()) item->Stop();
}

void MainWindow::redownloadSelected() {
    const auto item = selectedItem();
    if (!item) return;
    if (item->IsRunning()) return;

    const QString name = ToQ(item->GetFileName());
    if (ShowQuestion(
            this, AppTitle(),
            Tr("Re-download \"%1\" and overwrite the existing file?")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // The engine discards the completed state when Start() runs on a finished
    // item, rebuilds the segments and overwrites the destination on assembly.
    item->Start(DownloadManager::Instance().Settings());
    refresh(false);
}

void MainWindow::showFindDialog() {
    QInputDialog dialog(this);
    dialog.setWindowTitle(AppTitle());
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setLabelText(Tr("Find download:"));
    dialog.setTextValue(findText_);
    dialog.setOkButtonText(Tr("OK"));
    dialog.setCancelButtonText(Tr("Cancel"));
    if (dialog.exec() != QDialog::Accepted) return;

    findText_ = dialog.textValue().trimmed();
    findRow_ = -1;
    actFindNext_->setEnabled(!findText_.isEmpty() && table_->rowCount() > 0);
    if (findText_.isEmpty()) return;
    findNextMatch();
}

void MainWindow::findNextMatch() {
    if (findText_.isEmpty()) {
        showFindDialog();
        return;
    }

    const int rows = table_->rowCount();
    if (rows == 0) return;

    int start = findRow_ >= 0 ? findRow_ + 1 : table_->currentRow();
    if (start < 0 || start >= rows) start = 0;

    for (int offset = 0; offset < rows; ++offset) {
        const int row = (start + offset) % rows;
        bool match = false;
        for (int column : { ColFile, ColDescription }) {
            const QTableWidgetItem* cell = table_->item(row, column);
            if (cell && cell->text().contains(findText_, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        if (!match) continue;

        findRow_ = row;
        table_->setCurrentCell(row, ColFile);
        table_->selectRow(row);
        table_->scrollToItem(table_->item(row, ColFile),
                             QAbstractItemView::EnsureVisible);
        return;
    }

    ShowInfo(this, AppTitle(),
             Tr("Cannot find \"%1\".").arg(findText_));
}

void MainWindow::setSpeedLimit(int kb) {
    DownloadManager& manager = DownloadManager::Instance();
    manager.MutableSettings().speedLimitKB = std::max(0, kb);
    manager.SaveSettings();
}

void MainWindow::removeSelected(bool deleteFile) {
    const auto item = selectedItem();
    if (!item) return;

    const QString name = ToQ(item->GetFileName());
    const QString question =
        deleteFile
            ? Tr("Delete \"%1\" from the list and remove the downloaded "
                 "file from disk?")
                  .arg(name)
            : Tr("Remove \"%1\" from the list?").arg(name);

    if (ShowQuestion(this, AppTitle(), question,
                     QMessageBox::Yes | QMessageBox::No,
                     QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    DownloadManager::Instance().Remove(item->id, deleteFile);
    refresh(true);
}

void MainWindow::removeCompleted() {
    DownloadManager& manager = DownloadManager::Instance();
    const auto items = manager.Snapshot();

    int completed = 0;
    for (const auto& item : items) {
        if (item->GetState() == DownloadState::Complete) ++completed;
    }
    if (completed == 0) {
        ShowInfo(this, AppTitle(),
                 Tr("There are no completed downloads to delete."));
        return;
    }

    QString question;
    if (CurrentLanguage() == UiLanguage::Arabic) {
        question = Tr("Remove %1 completed downloads from the list?\n\n"
                      "The downloaded files are kept on disk.")
                       .arg(completed);
    } else {
        question = QStringLiteral(
            "Remove %1 completed download%2 from the list?\n\n"
            "The downloaded files are kept on disk.")
            .arg(completed)
            .arg(completed == 1 ? QString() : QStringLiteral("s"));
    }
    if (ShowQuestion(this, AppTitle(), question,
                     QMessageBox::Yes | QMessageBox::No,
                     QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    for (const auto& item : items) {
        if (item->GetState() == DownloadState::Complete) {
            manager.Remove(item->id, false);
        }
    }
    refresh(true);
}

void MainWindow::openSelectedFile() {
    const auto item = selectedItem();
    if (!item) return;

    const std::wstring path = item->GetSavePath();
    if (item->GetState() != DownloadState::Complete || !FileExists(path)) {
        ShowInfo(this, AppTitle(), Tr("The file has not been downloaded yet."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(ToQ(path)));
}

void MainWindow::openSelectedFolder() {
    const auto item = selectedItem();
    if (!item) return;

    const std::wstring path = item->GetSavePath();
    if (FileExists(path)) {
        const QString native = QDir::toNativeSeparators(ToQ(path));
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                { QStringLiteral("/select,"), native });
    } else {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(ToQ(DirName(path))));
    }
}

void MainWindow::copySelectedUrl() {
    if (const auto item = selectedItem()) {
        QApplication::clipboard()->setText(ToQ(item->url));
    }
}

void MainWindow::showOptions() {
    DownloadManager& manager = DownloadManager::Instance();
    Config cfg = manager.Settings();
    if (!ShowOptionsDialogQt(this, cfg)) return;

    manager.MutableSettings() = cfg;
    manager.SaveSettings();
    EnsureDirectory(cfg.tempDir);
    refresh(false);
}

void MainWindow::showHelp() {
    ShowInfo(this, AppTitle(), Tr(
        "FLEX Download Manager - quick help\n\n"
        "Add a download with Ctrl+N or the Add URL button.\n"
        "Resume / Stop control the selected download; the queue buttons "
        "act on every item.\n"
        "Minimizing or closing hides the window to the notification area.\n"
        "Use Exit in the tray menu (or Ctrl+Q) to quit completely."));
}

void MainWindow::toggleCategories(bool visible) {
    if (!categoriesPanel_) return;
    categoriesPanel_->setVisible(visible);
    if (visible) {
        categoriesPanel_->setMinimumWidth(130);
        splitter_->setSizes({ 190, 900 });
    }
}

// ---------------------------------------------------------------------------
// Closing
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent* event) {
    // The window close button and Alt+F4 only hide the window while a tray
    // icon is available. A full quit requires an explicit request from the
    // tray menu or File > Exit (Ctrl+Q).
    if (!quitRequested_ && tray_ && tray_->isVisible()) {
        event->ignore();
        hideToTray();
        return;
    }

    DownloadManager& manager = DownloadManager::Instance();
    const auto items = manager.Snapshot();

    bool active = false;
    for (const auto& item : items) {
        if (item->IsRunning()) {
            active = true;
            break;
        }
    }

    if (active) {
        if (!isVisible()) restoreFromTray();
        const auto answer = ShowQuestion(
            this, AppTitle(),
            Tr("Downloads are still running. Stop them and exit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            quitRequested_ = false;
            event->ignore();
            return;
        }
    }

    closing_ = true;
    manager.StopAll();
    if (tray_) tray_->hide();
    event->accept();
    QTimer::singleShot(0, qApp, [] { QApplication::quit(); });
}
