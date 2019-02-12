#include "NetworkSettingsPage.h"

#include "ui_NetworkSettingsPage.h"

#include "ApplicationSettings.h"

enum EnvConfiguration
{
   CustomConfiguration,
   StagingConfiguration,
   UATConfiguration,
   PRODConfiguration
};

struct EnvSettings
{
   QString  pubHost;
   int      pubPort;
};

bool operator == (const EnvSettings& l, const EnvSettings& r)
{
   return l.pubHost == r.pubHost
      && l.pubPort == r.pubPort;
}

static const EnvSettings StagingEnvSettings {
   QLatin1String("185.213.153.45"), 9091 };

static const EnvSettings UATEnvSettings{
   QLatin1String("185.213.153.44"), 9091 };

static const EnvSettings PRODEnvSettings{
   QLatin1String("185.213.153.36"), 9091 };

NetworkSettingsPage::NetworkSettingsPage(QWidget* parent)
   : SettingsPage{parent}
   , ui_{new Ui::NetworkSettingsPage}
{
   ui_->setupUi(this);

   ui_->comboBoxEnv->addItem(tr("Custom"));
   ui_->comboBoxEnv->addItem(tr("Staging"));
   ui_->comboBoxEnv->addItem(tr("UAT"));
   ui_->comboBoxEnv->addItem(tr("PROD"));

   ui_->comboBoxEnv->setCurrentIndex(-1);
   ui_->comboBoxEnv->setEnabled(false);

   connect(ui_->runArmoryDBLocallyCheckBox, &QCheckBox::clicked
      , this, &NetworkSettingsPage::onRunArmoryLocallyChecked);

   connect(ui_->checkBoxTestnet, &QCheckBox::clicked
      , this, &NetworkSettingsPage::onNetworkClicked);

   connect(ui_->lineEditPublicBridgeHost, &QLineEdit::textEdited, this, &NetworkSettingsPage::onEnvSettingsChanged);
   connect(ui_->spinBoxPublicBridgePort, SIGNAL(valueChanged(int)), this, SLOT(onEnvSettingsChanged()));

   connect(ui_->comboBoxEnv, SIGNAL(currentIndexChanged(int)), this, SLOT(onEnvSelected(int)));

   connect(ui_->armoryDBHostLineEdit, &QLineEdit::editingFinished, this, &NetworkSettingsPage::onArmoryHostChanged);
   connect(ui_->armoryDBPortLineEdit, &QLineEdit::editingFinished, this, &NetworkSettingsPage::onArmoryPortChanged);
}

NetworkSettingsPage::~NetworkSettingsPage() = default;

void NetworkSettingsPage::display()
{
   ui_->checkBoxTestnet->setChecked(appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet);

   if (appSettings_->get<bool>(ApplicationSettings::runArmoryLocally)) {
      ui_->runArmoryDBLocallyCheckBox->setChecked(true);
      DisplayRunArmorySettings(true);
   } else {
      ui_->runArmoryDBLocallyCheckBox->setChecked(false);
      DisplayRunArmorySettings(false);
   }

   ui_->lineEditPublicBridgeHost->setText(appSettings_->get<QString>(ApplicationSettings::pubBridgeHost));
   ui_->spinBoxPublicBridgePort->setValue(appSettings_->get<int>(ApplicationSettings::pubBridgePort));

   ui_->comboBoxEnv->setEnabled(true);
   DetectEnvironmentSettings();
}

void NetworkSettingsPage::reset()
{
   for (const auto &setting : { ApplicationSettings::runArmoryLocally, ApplicationSettings::netType
      , ApplicationSettings::pubBridgeHost, ApplicationSettings::pubBridgePort
      , ApplicationSettings::armoryDbIp, ApplicationSettings::armoryDbPort}) {
      appSettings_->reset(setting, false);
   }
   display();
}

void NetworkSettingsPage::onArmoryHostChanged()
{
   appSettings_->set(ApplicationSettings::armoryDbIp, ui_->armoryDBHostLineEdit->text(), false);
}

void NetworkSettingsPage::onArmoryPortChanged()
{
   appSettings_->set(ApplicationSettings::armoryDbPort, ui_->armoryDBPortLineEdit->text(), false);
}

void NetworkSettingsPage::DisplayRunArmorySettings(bool runLocally)
{
   auto networkType = ui_->checkBoxTestnet->isChecked()
      ? NetworkType::TestNet
      : NetworkType::MainNet;

   if (runLocally) {
      ui_->armoryDBHostLineEdit->setText(QString::fromStdString("localhost"));

      ui_->armoryDBPortLineEdit->setText(QString::number(appSettings_->GetDefaultArmoryLocalPort(networkType)));
      ui_->armoryDBHostLineEdit->setEnabled(false);
      ui_->armoryDBPortLineEdit->setEnabled(false);
   } else {
      ui_->armoryDBHostLineEdit->setEnabled(true);
      ui_->armoryDBPortLineEdit->setEnabled(true);
      ui_->armoryDBHostLineEdit->setText(appSettings_->get<QString>(ApplicationSettings::armoryDbIp));
      ui_->armoryDBPortLineEdit->setText(appSettings_->GetArmoryRemotePort(networkType));
   }
}

void NetworkSettingsPage::DetectEnvironmentSettings()
{
   int index = CustomConfiguration;

   EnvSettings currentConfiguration{
      ui_->lineEditPublicBridgeHost->text(),
      ui_->spinBoxPublicBridgePort->value()
   };

   if (currentConfiguration == StagingEnvSettings) {
      index = StagingConfiguration;
   } else if (currentConfiguration == UATEnvSettings) {
      index = UATConfiguration;
   } else if (currentConfiguration == PRODEnvSettings) {
      index = PRODConfiguration;
   }

   ui_->comboBoxEnv->setCurrentIndex(index);
}

void NetworkSettingsPage::apply()
{
   appSettings_->set(ApplicationSettings::netType, ui_->checkBoxTestnet->isChecked()
      ? (int)NetworkType::TestNet : (int)NetworkType::MainNet);

   if (ui_->runArmoryDBLocallyCheckBox->isChecked()) {
      appSettings_->set(ApplicationSettings::runArmoryLocally, true);
   } else {
      appSettings_->set(ApplicationSettings::runArmoryLocally, false);
      appSettings_->set(ApplicationSettings::armoryDbIp, ui_->armoryDBHostLineEdit->text());
      appSettings_->set(ApplicationSettings::armoryDbPort, ui_->armoryDBPortLineEdit->text());
   }

   appSettings_->set(ApplicationSettings::pubBridgeHost, ui_->lineEditPublicBridgeHost->text());
   appSettings_->set(ApplicationSettings::pubBridgePort, ui_->spinBoxPublicBridgePort->value());
}

void NetworkSettingsPage::onRunArmoryLocallyChecked(bool checked)
{
   DisplayRunArmorySettings(checked);
}

void NetworkSettingsPage::onNetworkClicked(bool)
{
   DisplayRunArmorySettings(ui_->runArmoryDBLocallyCheckBox->isChecked());
}

void NetworkSettingsPage::onEnvSettingsChanged()
{
   DetectEnvironmentSettings();
}

void NetworkSettingsPage::onEnvSelected(int index)
{
   if (index != CustomConfiguration) {
      const EnvSettings *settings = nullptr;
      if (index == StagingConfiguration) {
         settings = &StagingEnvSettings;
      } else if (index == UATConfiguration) {
         settings = &UATEnvSettings;
      } else  {
         settings = &PRODEnvSettings;
      }

      ui_->lineEditPublicBridgeHost->setText(settings->pubHost);
      ui_->spinBoxPublicBridgePort->setValue(settings->pubPort);
   }
}
