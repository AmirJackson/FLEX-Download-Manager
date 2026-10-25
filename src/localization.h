#pragma once

#include <QMessageBox>
#include <QString>

class QWidget;

// ---------------------------------------------------------------------------
// Runtime UI language
// ---------------------------------------------------------------------------
enum class UiLanguage {
    English = 0,
    Arabic  = 1,
};

UiLanguage CurrentLanguage();
void SetCurrentLanguage(UiLanguage language);

// Looks up the Arabic text for an English source string. English mode (or a
// missing translation) returns the source unchanged.
QString Tr(const char* english);
QString Tr(const QString& english);

// Message boxes whose standard buttons follow the selected language. The
// static QMessageBox helpers cannot be retranslated, so the UI uses these.
void ShowInfo(QWidget* parent, const QString& title, const QString& text);
void ShowWarning(QWidget* parent, const QString& title, const QString& text);
void ShowCritical(QWidget* parent, const QString& title, const QString& text);
QMessageBox::StandardButton ShowQuestion(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
    QMessageBox::StandardButton defaultButton = QMessageBox::No);
