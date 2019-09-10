// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The DogeCash developers
// Copyright (c) 2015-2019 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "askpassphrasedialog.h"
#include "ui_askpassphrasedialog.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "qt/dogecash/qtutils.h"
#include "qt/dogecash/loadingdialog.h"
#include "qt/dogecash/defaultdialog.h"
#include "qt/dogecash/dogecashgui.h"
#include <QDebug>

#include "allocators.h"

#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

AskPassphraseDialog::AskPassphraseDialog(Mode mode, QWidget* parent, WalletModel* model, Context context) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                                                                            ui(new Ui::AskPassphraseDialog),
                                                                                                            mode(mode),
                                                                                                            model(model),
                                                                                                            context(context),
                                                                                                            fCapsLock(false)
{
    ui->setupUi(this);
    this->setStyleSheet(GUIUtil::loadStyleSheet());

    ui->passEdit1->setMinimumSize(ui->passEdit1->sizeHint());
    ui->passEdit2->setMinimumSize(ui->passEdit2->sizeHint());
    ui->passEdit3->setMinimumSize(ui->passEdit3->sizeHint());

    ui->passEdit1->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit2->setMaxLength(MAX_PASSPHRASE_SIZE);
    ui->passEdit3->setMaxLength(MAX_PASSPHRASE_SIZE);

    // Setup Caps Lock detection.
    ui->passEdit1->installEventFilter(this);
    ui->passEdit2->installEventFilter(this);
    ui->passEdit3->installEventFilter(this);

    this->model = model;

    switch (mode) {
    case Mode::Encrypt: // Ask passphrase x2
        ui->warningLabel->setText(tr("Enter the new passphrase to the wallet.<br/>Please use a passphrase of <b>ten or more random characters</b>, or <b>eight or more words</b>."));
        ui->passLabel1->hide();
        ui->passEdit1->hide();
        setWindowTitle(tr("Encrypt wallet"));
        break;
    case Mode::UnlockAnonymize:
        ui->anonymizationCheckBox->show();
    case Mode::Unlock: // Ask passphrase
        ui->warningLabel->setText(tr("This operation needs your wallet passphrase to unlock the wallet."));
        ui->passLabel2->hide();
        ui->passEdit2->hide();
        ui->passLabel3->hide();
        ui->passEdit3->hide();
        setWindowTitle(tr("Unlock wallet"));
        break;
    case Mode::Decrypt: // Ask passphrase
        ui->warningLabel->setText(tr("This operation needs your wallet passphrase to decrypt the wallet."));
        ui->passLabel2->hide();
        ui->passEdit2->hide();
        ui->passLabel3->hide();
        ui->passEdit3->hide();
        setWindowTitle(tr("Decrypt wallet"));
        break;
    case Mode::ChangePass: // Ask old passphrase + new passphrase x2
        setWindowTitle(tr("Change passphrase"));
        ui->warningLabel->setText(tr("Enter the old and new passphrase to the wallet."));
        break;
    }

    // Set checkbox "For anonymization, automint, and staking only" depending on from where we were called
    if (context == Context::Unlock_Menu || context == Context::Mint_zdogec || context == Context::BIP_38 || context == Context::UI_Vote) {
        ui->anonymizationCheckBox->setChecked(true);
    }
    else {
        ui->anonymizationCheckBox->setChecked(false);
    }

    // It doesn't make sense to show the checkbox for sending DOGEC because you wouldn't check it anyway.
    if (context == Context::Send_DOGEC || context == Context::Send_zdogec) {
        ui->anonymizationCheckBox->hide();
    }

    textChanged();
    connect(ui->passEdit1, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit2, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
    connect(ui->passEdit3, SIGNAL(textChanged(QString)), this, SLOT(textChanged()));
}

AskPassphraseDialog::~AskPassphraseDialog()
{
    // Attempt to overwrite text so that they do not linger around in memory
    ui->passEdit1->setText(QString(" ").repeated(ui->passEdit1->text().size()));
    ui->passEdit2->setText(QString(" ").repeated(ui->passEdit2->text().size()));
    ui->passEdit3->setText(QString(" ").repeated(ui->passEdit3->text().size()));
    delete ui;
}

void AskPassphraseDialog::accept()
{
    SecureString oldpass, newpass1, newpass2;
    if (!model)
        return;
    oldpass.reserve(MAX_PASSPHRASE_SIZE);
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    newpass2.reserve(MAX_PASSPHRASE_SIZE);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make this input mlock()'d to begin with.
    oldpass.assign(ui->passEdit1->text().toStdString().c_str());
    newpass1.assign(ui->passEdit2->text().toStdString().c_str());
    newpass2.assign(ui->passEdit3->text().toStdString().c_str());

    switch (mode) {
    case Mode::Encrypt: {
        if (newpass1.empty() || newpass2.empty()) {
            // Cannot encrypt with empty passphrase
            break;
        }
        hide();
        bool ret = openStandardDialog(
                tr("Confirm wallet encryption"),
                tr("Warning: If you encrypt your wallet and lose your passphrase, you will <b>LOSE ALL OF YOUR DOGEC</b>!") + "<br><br>" + tr("Are you sure you wish to encrypt your wallet?"),
                tr("ENCRYPT"), tr("CANCEL")
        );
        if (ret) {
            if (newpass1 == newpass2) {
                newpassCache = newpass1;
                DogeCashGUI* window = static_cast<DogeCashGUI*>(parentWidget());
                LoadingDialog *dialog = new LoadingDialog(window);
                dialog->execute(this, 1);
                openDialogWithOpaqueBackgroundFullScreen(dialog, window);
            } else {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                    tr("The supplied passphrases do not match."));
            }
        } else {
            QDialog::reject(); // Cancelled
        }
    } break;
    case Mode::UnlockAnonymize:
    case Mode::Unlock:
        if (!model->setWalletLocked(false, oldpass, ui->anonymizationCheckBox->isChecked())) {
            QMessageBox::critical(this, tr("Wallet unlock failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."));
        } else {
            QDialog::accept(); // Success
        }
        break;
    case Mode::Decrypt:
        if (!model->setWalletEncrypted(false, oldpass)) {
            QMessageBox::critical(this, tr("Wallet decryption failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."));
        } else {
            QDialog::accept(); // Success
        }
        break;
    case Mode::ChangePass:
        if (newpass1 == newpass2) {
            if (model->changePassphrase(oldpass, newpass1)) {
                QMessageBox::information(this, tr("Wallet encrypted"),
                    tr("Wallet passphrase was successfully changed."));
                QDialog::accept(); // Success
            } else {
                QMessageBox::critical(this, tr("Wallet encryption failed"),
                    tr("The passphrase entered for the wallet decryption was incorrect."));
            }
        } else {
            QMessageBox::critical(this, tr("Wallet encryption failed"),
                tr("The supplied passphrases do not match."));
        }
        break;
    }
}

void AskPassphraseDialog::textChanged()
{
    // Validate input, set Ok button to enabled when acceptable
    bool acceptable = false;
    switch (mode) {
    case Mode::Encrypt: // New passphrase x2
        acceptable = !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    case Mode::UnlockAnonymize: // Old passphrase x1
    case Mode::Unlock:          // Old passphrase x1
    case Mode::Decrypt:
        acceptable = !ui->passEdit1->text().isEmpty();
        break;
    case Mode::ChangePass: // Old passphrase x1, new passphrase x2
        acceptable = !ui->passEdit1->text().isEmpty() && !ui->passEdit2->text().isEmpty() && !ui->passEdit3->text().isEmpty();
        break;
    }
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(acceptable);
}

bool AskPassphraseDialog::event(QEvent* event)
{
    // Detect Caps Lock key press.
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_CapsLock) {
            fCapsLock = !fCapsLock;
        }
        if (fCapsLock) {
            ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
        } else {
            ui->capsLabel->clear();
        }
    }
    return QWidget::event(event);
}

bool AskPassphraseDialog::eventFilter(QObject* object, QEvent* event)
{
    /* Detect Caps Lock.
     * There is no good OS-independent way to check a key state in Qt, but we
     * can detect Caps Lock by checking for the following condition:
     * Shift key is down and the result is a lower case character, or
     * Shift key is not down and the result is an upper case character.
     */
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        QString str = ke->text();
        if (str.length() != 0) {
            const QChar* psz = str.unicode();
            bool fShift = (ke->modifiers() & Qt::ShiftModifier) != 0;
            if ((fShift && *psz >= 'a' && *psz <= 'z') || (!fShift && *psz >= 'A' && *psz <= 'Z')) {
                fCapsLock = true;
                ui->capsLabel->setText(tr("Warning: The Caps Lock key is on!"));
            } else if (psz->isLetter()) {
                fCapsLock = false;
                ui->capsLabel->clear();
            }
        }
    }
    return QDialog::eventFilter(object, event);
}

bool AskPassphraseDialog::openStandardDialog(QString title, QString body, QString okBtn, QString cancelBtn){
    DogeCashGUI* gui = static_cast<DogeCashGUI*>(parentWidget());
    DefaultDialog *confirmDialog = new DefaultDialog(gui);
    confirmDialog->setText(title, body, okBtn, cancelBtn);
    confirmDialog->adjustSize();
    openDialogWithOpaqueBackground(confirmDialog, gui);
    bool ret = confirmDialog->isOk;
    confirmDialog->deleteLater();
    return ret;
}

void AskPassphraseDialog::warningMessage() {
    hide();
    static_cast<DogeCashGUI*>(parentWidget())->showHide(true);
    openStandardDialog(
            tr("Wallet encrypted"),
            "<qt>" +
            tr("DogeCash will close now to finish the encryption process. "
               "Remember that encrypting your wallet cannot fully protect "
               "your DOGECs from being stolen by malware infecting your computer.") +
            "<br><br><b>" +
            tr("IMPORTANT: Any previous backups you have made of your wallet file "
               "should be replaced with the newly generated, encrypted wallet file. "
               "For security reasons, previous backups of the unencrypted wallet file "
               "will become useless as soon as you start using the new, encrypted wallet.") +
            "</b></qt>",
            tr("OK")
            );
    QApplication::quit();
}

void AskPassphraseDialog::errorEncryptingWallet() {
    QMessageBox::critical(this, tr("Wallet encryption failed"),
                          tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."));
}

void AskPassphraseDialog::run(int type){
    if (type == 1) {
        if (!newpassCache.empty()) {
            QMetaObject::invokeMethod(this, "hide", Qt::QueuedConnection);
            if (model->setWalletEncrypted(true, newpassCache)) {
                QMetaObject::invokeMethod(this, "warningMessage", Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, "errorEncryptingWallet", Qt::QueuedConnection);
            }
            newpassCache.clear();
            QDialog::accept(); // Success
        }
    }
}
void AskPassphraseDialog::onError(int type, QString error){
    newpassCache = "";
}

void AskPassphraseDialog::initWatch(QWidget *parent) {
    btnWatch = new QCheckBox(parent);
    setCssProperty(btnWatch, "btn-watch-password");
    btnWatch->setChecked(false);
    QSize BUTTON_CONTACT_SIZE = QSize(24, 24);
    btnWatch->setMinimumSize(BUTTON_CONTACT_SIZE);
    btnWatch->setMaximumSize(BUTTON_CONTACT_SIZE);
    btnWatch->show();
    btnWatch->raise();

    int posYY = 8;
    btnWatch->move(450, posYY);
}