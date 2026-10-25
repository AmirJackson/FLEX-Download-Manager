#include "localization.h"

#include <QAbstractButton>
#include <QApplication>
#include <QHash>
#include <QPushButton>

namespace {

UiLanguage g_language = UiLanguage::English;

QString Ar(const char* text) {
    return QString::fromUtf8(text);
}

const QHash<QString, QString>& ArabicStrings() {
    static const QHash<QString, QString> table = {
        // Menu bar ---------------------------------------------------------
        { QStringLiteral("&Tasks"), Ar("المهام") },
        { QStringLiteral("&File"), Ar("ملف") },
        { QStringLiteral("&Downloads"), Ar("التنزيلات") },
        { QStringLiteral("&View"), Ar("عرض") },
        { QStringLiteral("&Help"), Ar("مساعدة") },
        { QStringLiteral("&Registration"), Ar("التسجيل") },

        // Tasks menu / actions --------------------------------------------
        { QStringLiteral("Add new download..."), Ar("إضافة تنزيل جديد...") },
        { QStringLiteral("Add URL..."), Ar("إضافة رابط...") },
        { QStringLiteral("Add URL"), Ar("إضافة رابط") },
        { QStringLiteral("Add from clipboard"), Ar("إضافة من الحافظة") },
        { QStringLiteral("Exit"), Ar("خروج") },

        // Download commands -----------------------------------------------
        { QStringLiteral("Download Now"), Ar("تنزيل الآن") },
        { QStringLiteral("Resume"), Ar("استئناف") },
        { QStringLiteral("Stop Download"), Ar("إيقاف التنزيل") },
        { QStringLiteral("Stop"), Ar("إيقاف") },
        { QStringLiteral("Redownload"), Ar("إعادة التنزيل") },
        { QStringLiteral("Start all"), Ar("بدء الكل") },
        { QStringLiteral("Stop All"), Ar("إيقاف الكل") },
        { QStringLiteral("Pause All"), Ar("إيقاف الكل مؤقتاً") },
        { QStringLiteral("Start Queue"), Ar("بدء قائمة الانتظار") },
        { QStringLiteral("Stop Queue"), Ar("إيقاف قائمة الانتظار") },
        { QStringLiteral("Remove"), Ar("إزالة") },
        { QStringLiteral("Delete"), Ar("حذف") },
        { QStringLiteral("Delete with file"), Ar("حذف مع الملف") },
        { QStringLiteral("Delete All Completed"), Ar("حذف كل المكتملة") },
        { QStringLiteral("Delete Completed"), Ar("حذف المكتملة") },
        { QStringLiteral("Open file"), Ar("فتح الملف") },
        { QStringLiteral("Open folder"), Ar("فتح المجلد") },
        { QStringLiteral("Copy URL"), Ar("نسخ الرابط") },
        { QStringLiteral("Select all"), Ar("تحديد الكل") },
        { QStringLiteral("Find"), Ar("بحث") },
        { QStringLiteral("Find Next"), Ar("بحث عن التالي") },
        { QStringLiteral("Options"), Ar("خيارات") },
        { QStringLiteral("Scheduler"), Ar("الجدولة") },
        { QStringLiteral("Tell a friend"), Ar("أخبر صديقاً") },
        { QStringLiteral("Tell a Friend"), Ar("أخبر صديقاً") },
        { QStringLiteral("Start queue"), Ar("بدء قائمة الانتظار") },
        { QStringLiteral("Stop queue"), Ar("إيقاف قائمة الانتظار") },
        { QStringLiteral("Main queue"), Ar("قائمة الانتظار الرئيسية") },
        { QStringLiteral("Speed Limiter"), Ar("محدّد السرعة") },
        { QStringLiteral("Turn off"), Ar("إيقاف التحديد") },
        { QStringLiteral("%1 KB/s"), Ar("%1 كيلوبايت/ث") },
        { QStringLiteral("100 KB/s"), Ar("100 كيلوبايت/ث") },
        { QStringLiteral("250 KB/s"), Ar("250 كيلوبايت/ث") },
        { QStringLiteral("500 KB/s"), Ar("500 كيلوبايت/ث") },
        { QStringLiteral("1000 KB/s"), Ar("1000 كيلوبايت/ث") },
        { QStringLiteral("2000 KB/s"), Ar("2000 كيلوبايت/ث") },
        { QStringLiteral("Custom..."), Ar("مخصص...") },

        // View menu --------------------------------------------------------
        { QStringLiteral("Toolbar"), Ar("شريط الأدوات") },
        { QStringLiteral("Status bar"), Ar("شريط الحالة") },
        { QStringLiteral("Categories"), Ar("الفئات") },
        { QStringLiteral("Refresh"), Ar("تحديث") },
        { QStringLiteral("Arrange files"), Ar("ترتيب الملفات") },
        { QStringLiteral("Language"), Ar("اللغة") },
        { QStringLiteral("By Order Of Addition"), Ar("حسب ترتيب الإضافة") },
        { QStringLiteral("By File Name"), Ar("حسب اسم الملف") },
        { QStringLiteral("By Size"), Ar("حسب الحجم") },
        { QStringLiteral("By Status"), Ar("حسب الحالة") },
        { QStringLiteral("By Time Left"), Ar("حسب الوقت المتبقي") },
        { QStringLiteral("By Transfer Rate"), Ar("حسب سرعة النقل") },
        { QStringLiteral("By Last Try Date"), Ar("حسب تاريخ آخر محاولة") },
        { QStringLiteral("By Description"), Ar("حسب الوصف") },
        { QStringLiteral("By Save Path"), Ar("حسب مسار الحفظ") },
        { QStringLiteral("By Referer"), Ar("حسب المُحيل") },
        { QStringLiteral("By Parent Web Page"), Ar("حسب صفحة الويب الأصلية") },

        // Help / registration ---------------------------------------------
        { QStringLiteral("Help..."), Ar("مساعدة...") },
        { QStringLiteral("About..."), Ar("حول...") },
        { QStringLiteral("Register..."), Ar("تسجيل...") },
        { QStringLiteral("Registration info..."), Ar("معلومات التسجيل...") },

        // Categories -------------------------------------------------------
        { QStringLiteral("All Downloads"), Ar("كل التنزيلات") },
        { QStringLiteral("Compressed"), Ar("المضغوطة") },
        { QStringLiteral("Documents"), Ar("المستندات") },
        { QStringLiteral("Music"), Ar("الموسيقى") },
        { QStringLiteral("Programs"), Ar("البرامج") },
        { QStringLiteral("Video"), Ar("الفيديو") },
        { QStringLiteral("Unfinished"), Ar("غير المكتملة") },
        { QStringLiteral("Finished"), Ar("المكتملة") },
        { QStringLiteral("Grabber projects"), Ar("مشاريع الالتقاط") },
        { QStringLiteral("Queues"), Ar("قوائم الانتظار") },
        { QStringLiteral("Hide categories"), Ar("إخفاء الفئات") },

        // Table ------------------------------------------------------------
        { QStringLiteral("File Name"), Ar("اسم الملف") },
        { QStringLiteral("Q"), Ar("Q") },
        { QStringLiteral("Size"), Ar("الحجم") },
        { QStringLiteral("Status"), Ar("الحالة") },
        { QStringLiteral("Time left"), Ar("الوقت المتبقي") },
        { QStringLiteral("Transfer rate"), Ar("سرعة النقل") },
        { QStringLiteral("Last Try Date"), Ar("تاريخ آخر محاولة") },
        { QStringLiteral("Description"), Ar("الوصف") },

        // Download states --------------------------------------------------
        { QStringLiteral("Queued"), Ar("في الانتظار") },
        { QStringLiteral("Connecting..."), Ar("جارٍ الاتصال...") },
        { QStringLiteral("Downloading"), Ar("جارٍ التنزيل") },
        { QStringLiteral("Paused"), Ar("متوقف مؤقتاً") },
        { QStringLiteral("Complete"), Ar("مكتمل") },
        { QStringLiteral("Error"), Ar("خطأ") },
        { QStringLiteral("Cancelled"), Ar("ملغى") },
        { QStringLiteral("Unknown"), Ar("غير معروف") },

        // Status bar -------------------------------------------------------
        { QStringLiteral("Ready. Use Add URL or paste a link to start downloading."),
          Ar("جاهز. استخدم إضافة رابط أو الصق رابطاً لبدء التنزيل.") },
        { QStringLiteral("%1 active, %2 completed, %3 failed - %4 total"),
          Ar("%1 نشط، %2 مكتمل، %3 فشل - %4 الإجمالي") },

        // Messages ---------------------------------------------------------
        { QStringLiteral("The scheduler is not implemented in this personal "
                         "build. Use Start queue / Stop queue instead."),
          Ar("الجدولة غير مطبقة في هذه النسخة الشخصية. استخدم بدء قائمة "
             "الانتظار / إيقاف قائمة الانتظار بدلاً من ذلك.") },
        { QStringLiteral("We are living in the year 2077, do you think you still need to use paid software?"),
          Ar("نحن نعيش في سنة 2077 هل تعتقد انك مازالت بحاجة لاستخدام برامج مدفوعة؟") },
        { QStringLiteral("This software is free and open source. If you obtained it from any place, website, entity, or person for a fee, or paid to use certain features in the software, this is a violation of the software's usage policy and you should request a refund immediately."),
          Ar("هذا البرنامج مجاني ومفتوح المصدر فاذا حصلت عليه من اي مكان او موقع او جهة او شخص بطريقة مدفوعة او انك دفعت مقابل استخدام مميزات معينة في البرنامج فهذا انتهاك لسياسة استخدام البرنامج وعليك استرداد اموالك فورا.") },
        { QStringLiteral("Could not create the download."),
          Ar("تعذر إنشاء التنزيل.") },
        { QStringLiteral("The clipboard does not contain a supported "
                         "http, https or ftp URL."),
          Ar("لا تحتوي الحافظة على رابط http أو https أو ftp مدعوم.") },
        { QStringLiteral("The clipboard is empty."), Ar("الحافظة فارغة.") },
        { QStringLiteral("Re-download \"%1\" and overwrite the existing "
                         "file?"),
          Ar("هل تريد إعادة تنزيل \"%1\" واستبدال الملف الموجود؟") },
        { QStringLiteral("Find download:"), Ar("البحث عن تنزيل:") },
        { QStringLiteral("Cannot find \"%1\"."),
          Ar("تعذر العثور على \"%1\".") },
        { QStringLiteral("Speed limit (0 = unlimited):"),
          Ar("حد السرعة (0 = بلا حد):") },
        { QStringLiteral("Delete \"%1\" from the list and remove the "
                         "downloaded file from disk?"),
          Ar("هل تريد حذف \"%1\" من القائمة وإزالة الملف المنزّل من القرص؟") },
        { QStringLiteral("Remove \"%1\" from the list?"),
          Ar("هل تريد إزالة \"%1\" من القائمة؟") },
        { QStringLiteral("There are no completed downloads to delete."),
          Ar("لا توجد تنزيلات مكتملة لحذفها.") },
        { QStringLiteral("Remove %1 completed downloads from the list?\n\n"
                         "The downloaded files are kept on disk."),
          Ar("هل تريد إزالة %1 من التنزيلات المكتملة من القائمة؟\n\n"
             "ستبقى الملفات المنزّلة على القرص.") },
        { QStringLiteral("The file has not been downloaded yet."),
          Ar("لم يتم تنزيل الملف بعد.") },
        { QStringLiteral("Downloads are still running. Stop them and exit?"),
          Ar("لا تزال هناك تنزيلات قيد التشغيل. هل تريد إيقافها والخروج؟") },
        { QStringLiteral("FLEX Download Manager is still running in the "
                         "notification area."),
          Ar("لا يزال FLEX Download Manager يعمل في منطقة الإشعارات.") },
        { QStringLiteral("Show main window"), Ar("إظهار النافذة الرئيسية") },
        { QStringLiteral(
             "FLEX Download Manager - quick help\n\n"
             "Add a download with Ctrl+N or the Add URL button.\n"
             "Resume / Stop control the selected download; the queue buttons "
             "act on every item.\n"
             "Minimizing or closing hides the window to the notification area.\n"
             "Use Exit in the tray menu (or Ctrl+Q) to quit completely."),
          Ar("FLEX Download Manager - مساعدة سريعة\n\n"
             "أضف تنزيلاً باستخدام Ctrl+N أو زر إضافة رابط.\n"
             "استئناف / إيقاف يتحكمان في التنزيل المحدد؛ أزرار قائمة "
             "الانتظار تعمل على كل العناصر.\n"
             "التصغير أو الإغلاق يخفي النافذة إلى منطقة الإشعارات.\n"
             "استخدم «خروج» من قائمة أيقونة النظام أو Ctrl+Q للإنهاء بالكامل.") },

        // Add download dialog ---------------------------------------------
        { QStringLiteral("Add download"), Ar("إضافة تنزيل") },
        { QStringLiteral("URL:"), Ar("الرابط:") },
        { QStringLiteral("Save to:"), Ar("الحفظ في:") },
        { QStringLiteral("Browse..."), Ar("استعراض...") },
        { QStringLiteral("File name:"), Ar("اسم الملف:") },
        { QStringLiteral("Leave empty to use the name from the URL"),
          Ar("اتركه فارغاً لاستخدام الاسم من الرابط") },
        { QStringLiteral("Connections:"), Ar("الاتصالات:") },
        { QStringLiteral("Start the download immediately"),
          Ar("بدء التنزيل فوراً") },
        { QStringLiteral("Enter a direct link. The server is probed for "
                         "byte-range support so the transfer can be split "
                         "across connections."),
          Ar("أدخل رابطاً مباشراً. يتم فحص الخادم لدعم النطاقات ليمكن تقسيم "
             "النقل عبر عدة اتصالات.") },
        { QStringLiteral("OK"), Ar("موافق") },
        { QStringLiteral("Cancel"), Ar("إلغاء") },
        { QStringLiteral("Please enter a valid http, https or ftp URL."),
          Ar("يرجى إدخال رابط http أو https أو ftp صالح.") },
        { QStringLiteral("The download folder could not be created."),
          Ar("تعذر إنشاء مجلد التنزيل.") },
        { QStringLiteral("Select download folder"), Ar("اختر مجلد التنزيل") },

        // Download File Info (Chrome integration) -------------------------
        { QStringLiteral("Download File Info"),
          Ar("معلومات ملف التنزيل") },
        { QStringLiteral("Category:"), Ar("الفئة:") },
        { QStringLiteral("Save As:"), Ar("الحفظ باسم:") },
        { QStringLiteral("Remember this path for \"%1\" category"),
          Ar("تذكّر هذا المسار لفئة \"%1\"") },
        { QStringLiteral("Description:"), Ar("الوصف:") },
        { QStringLiteral("Download Later"), Ar("تنزيل لاحقاً") },
        { QStringLiteral("Start Download"), Ar("بدء التنزيل") },
        { QStringLiteral("Unknown size"), Ar("حجم غير معروف") },
        { QStringLiteral("Add category"), Ar("إضافة فئة") },
        { QStringLiteral("Category name:"), Ar("اسم الفئة:") },
        { QStringLiteral("Other"), Ar("أخرى") },
        { QStringLiteral("Save"), Ar("حفظ") },
        { QStringLiteral("Save file as"), Ar("حفظ الملف باسم") },
        { QStringLiteral("All files (*.*)"), Ar("كل الملفات (*.*)") },
        { QStringLiteral("Chrome bridge could not start: %1"),
          Ar("تعذر بدء جسر Chrome: %1") },

        // Download progress window ----------------------------------------
        { QStringLiteral("Download progress"), Ar("تقدم التنزيل") },
        { QStringLiteral("Status:"), Ar("الحالة:") },
        { QStringLiteral("File size"), Ar("حجم الملف") },
        { QStringLiteral("Transfer rate"), Ar("معدل النقل") },
        { QStringLiteral("Downloaded"), Ar("تم تنزيله") },
        { QStringLiteral("Time left"), Ar("الوقت المتبقي") },
        { QStringLiteral("Resume capability"), Ar("إمكانية الاستئناف") },
        { QStringLiteral("Yes"), Ar("نعم") },
        { QStringLiteral("No"), Ar("لا") },
        { QStringLiteral("(unknown)"), Ar("(غير معروف)") },
        { QStringLiteral("<< Hide details"), Ar("إخفاء التفاصيل <<") },
        { QStringLiteral("Show details >>"), Ar("إظهار التفاصيل >>") },
        { QStringLiteral("Pause"), Ar("إيقاف مؤقت") },
        { QStringLiteral("Start"), Ar("بدء") },
        { QStringLiteral("Cancel download"), Ar("إلغاء التنزيل") },
        { QStringLiteral("Are you sure you want to cancel this download?"),
          Ar("هل أنت متأكد من إلغاء هذا التنزيل؟") },
        { QStringLiteral(
              "Start positions and download progress by connections"),
          Ar("مواضع البدء وتقدم التنزيل حسب الاتصالات") },
        { QStringLiteral("N."), Ar("ر.") },
        { QStringLiteral("Info"), Ar("معلومات") },
        { QStringLiteral("Done"), Ar("تم") },
        { QStringLiteral("Retrying"), Ar("إعادة المحاولة") },
        { QStringLiteral("Pending"), Ar("بالانتظار") },
        { QStringLiteral("External downloader"), Ar("برنامج تنزيل خارجي") },
        { QStringLiteral("External downloader (yt-dlp)"),
          Ar("برنامج تنزيل خارجي (yt-dlp)") },
        { QStringLiteral("Connections: %1 of %2"),
          Ar("الاتصالات: %1 من %2") },
        { QStringLiteral("%1 of %2 (%3%)"), Ar("%1 من %2 (%3%)") },
        { QStringLiteral("%1 - open ended"), Ar("%1 - مفتوح النهاية") },
        { QStringLiteral("File"), Ar("ملف") },
        { QStringLiteral("Show the Download progress dialog"),
          Ar("إظهار مربع حوار تقدم التنزيل") },
        { QStringLiteral("Show progress window"), Ar("إظهار نافذة التقدم") },

        // Download complete window ----------------------------------------
        { QStringLiteral("Download complete"), Ar("اكتمال التحميل") },
        { QStringLiteral("Downloaded %1 (%2 Bytes)"),
          Ar("تم تنزيل %1 (%2 بايت)") },
        { QStringLiteral("Address"), Ar("العنوان") },
        { QStringLiteral("The file saved as"), Ar("تم حفظ الملف باسم") },
        { QStringLiteral("Open"), Ar("فتح") },
        { QStringLiteral("Close"), Ar("إغلاق") },
        { QStringLiteral("Don't show this dialog again"),
          Ar("عدم إظهار مربع الحوار هذا مرة أخرى") },

        // Options dialog ---------------------------------------------------
        { QStringLiteral("Connection"), Ar("الاتصال") },
        { QStringLiteral("Retries:"), Ar("المحاولات:") },
        { QStringLiteral("Read timeout:"), Ar("مهلة القراءة:") },
        { QStringLiteral("Speed limit:"), Ar("حد السرعة:") },
        { QStringLiteral("Minimum part:"), Ar("الحد الأدنى للجزء:") },
        { QStringLiteral("User agent:"), Ar("وكيل المستخدم:") },
        { QStringLiteral("Folders"), Ar("المجلدات") },
        { QStringLiteral("Temporary folder:"), Ar("المجلد المؤقت:") },
        { QStringLiteral("Default download folder:"),
          Ar("مجلد التنزيل الافتراضي:") },
        { QStringLiteral("Proxy"), Ar("الوكيل") },
        { QStringLiteral("Mode:"), Ar("الوضع:") },
        { QStringLiteral("Direct connection"), Ar("اتصال مباشر") },
        { QStringLiteral("Use system proxy"), Ar("استخدام وكيل النظام") },
        { QStringLiteral("Manual proxy"), Ar("وكيل يدوي") },
        { QStringLiteral("Proxy address:"), Ar("عنوان الوكيل:") },
        { QStringLiteral("host:port"), Ar("المضيف:المنفذ") },
        { QStringLiteral("Settings are stored per user under "
                         "HKCU\\Software\\FLEXDownloadManager."),
          Ar("تُحفظ الإعدادات لكل مستخدم تحت HKCU\\Software\\FLEXDownloadManager.") },
        { QStringLiteral("Unlimited"), Ar("بلا حد") },
        { QStringLiteral("Select folder"), Ar("اختر مجلداً") },
        { QStringLiteral("Notifications"), Ar("الإشعارات") },
        { QStringLiteral("Show the Download complete dialog"),
          Ar("إظهار مربع اكتمال التحميل") },

        // About ------------------------------------------------------------
        { QStringLiteral("About FLEX Download Manager"), Ar("حول FLEX Download Manager") },
        { QStringLiteral(
             "An open source segmented download manager built with "
             "Qt Widgets and WinHTTP.\n\n"
             "Toolbar icons: Microsoft Fluent UI System Icons (MIT licence).\n\n"
             "Contributors to development: DeepSeek 4.1 Flash, Claude Opus 5.5, Gemini 3.8 Flash, and Gemini 4 Argon"),
          Ar("مدير تنزيل مجزأ مفتوح المصدر مبني باستخدام Qt Widgets "
             "وWinHTTP.\n\n"
             "أيقونات شريط الأدوات: Microsoft Fluent UI System Icons "
             "(رخصة MIT).\n\n"
             "المشاركون في التطوير: DeepSeek 4.1 Flash و Claude Opus 5.5 و Gemini 3.8 Flash و Gemini 4 Argon") },

        // Folder picker ----------------------------------------------------
        { QStringLiteral("Select"), Ar("اختيار") },
        { QStringLiteral("Look in:"), Ar("البحث في:") },
        { QStringLiteral("Folder:"), Ar("المجلد:") },
        { QStringLiteral("Files of type:"), Ar("أنواع الملفات:") },
    };
    return table;
}

void LocalizeMessageBox(QMessageBox& box) {
    const struct {
        QMessageBox::StandardButton button;
        const char* text;
    } entries[] = {
        { QMessageBox::Ok, "OK" },
        { QMessageBox::Cancel, "Cancel" },
        { QMessageBox::Yes, "Yes" },
        { QMessageBox::No, "No" },
        { QMessageBox::Close, "Close" },
        { QMessageBox::Abort, "Abort" },
        { QMessageBox::Retry, "Retry" },
        { QMessageBox::Ignore, "Ignore" },
    };
    for (const auto& entry : entries) {
        if (QAbstractButton* button = box.button(entry.button)) {
            button->setText(Tr(entry.text));
        }
    }
}

}  // namespace

QString Tr(const char* english) {
    return Tr(QString::fromUtf8(english));
}

QString Tr(const QString& english) {
    if (g_language == UiLanguage::English) return english;
    const auto& table = ArabicStrings();
    const auto it = table.constFind(english);
    return it == table.constEnd() ? english : it.value();
}

UiLanguage CurrentLanguage() {
    return g_language;
}

void SetCurrentLanguage(UiLanguage language) {
    g_language = language;
    if (qApp) {
        qApp->setLayoutDirection(language == UiLanguage::Arabic
                                     ? Qt::RightToLeft
                                     : Qt::LeftToRight);
    }
}

void ShowInfo(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok,
                    parent);
    LocalizeMessageBox(box);
    box.exec();
}

void ShowWarning(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    LocalizeMessageBox(box);
    box.exec();
}

void ShowCritical(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox box(QMessageBox::Critical, title, text, QMessageBox::Ok,
                    parent);
    LocalizeMessageBox(box);
    box.exec();
}

QMessageBox::StandardButton ShowQuestion(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton) {
    QMessageBox box(QMessageBox::Question, title, text, buttons, parent);
    if (auto* push = qobject_cast<QPushButton*>(box.button(defaultButton))) {
        box.setDefaultButton(push);
    }
    LocalizeMessageBox(box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}
