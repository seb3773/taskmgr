/*
 * about_dialog.cpp — Custom About dialog for taskmgr.
 */

#include "about_dialog.h"
#include "app_icons.h"

#include <ntqdialog.h>
#include <ntqlayout.h>
#include <ntqlabel.h>
#include <ntqpushbutton.h>
#include <ntqfont.h>

void showAboutDialog(TQWidget* parent)
{
    TQDialog dlg(parent, "about_dialog", true);
    dlg.setCaption("About TaskMgr");
    dlg.setBackgroundColor(TQt::white);
    dlg.setFixedWidth(360);

    TQVBoxLayout* layout = new TQVBoxLayout(&dlg, 16, 8);

    TQLabel* logo = new TQLabel(&dlg);
    logo->setAlignment(TQt::AlignHCenter);
    TQPixmap logoPix = embeddedTuxmgrAboutIcon();
    if (!logoPix.isNull())
        logo->setPixmap(logoPix);
    layout->addWidget(logo);

    TQLabel* title = new TQLabel("TaskMgr", &dlg);
    title->setAlignment(TQt::AlignHCenter);
    TQFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    TQLabel* subtitle = new TQLabel("A Task Manager for Trinity Desktop", &dlg);
    subtitle->setAlignment(TQt::AlignHCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(12);

    TQLabel* credit = new TQLabel("By seb3773 - https://github.com/seb3773", &dlg);
    credit->setAlignment(TQt::AlignHCenter);
    TQFont creditFont = credit->font();
    creditFont.setPointSize(creditFont.pointSize() - 2);
    credit->setFont(creditFont);
    layout->addWidget(credit);

    TQHBoxLayout* btnRow = new TQHBoxLayout(0, 0, 0);
    btnRow->addStretch(1);
    TQPushButton* okBtn = new TQPushButton("OK", &dlg);
    okBtn->setDefault(true);
    btnRow->addWidget(okBtn);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    TQObject::connect(okBtn, SIGNAL(clicked()), &dlg, SLOT(accept()));
    dlg.exec();
}
