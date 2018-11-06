#ifndef __TRANSACTIONS_WIDGET_UI_H__
#define __TRANSACTIONS_WIDGET_UI_H__

#include <QMenu>
#include <QWidget>

#include <memory>

#include "TabWithShortcut.h"

namespace Ui {
   class TransactionsWidget;
}
class ArmoryConnection;
class SignContainer;
class TransactionsProxy;
class TransactionsViewModel;
class TransactionsSortFilterModel;
class WalletsManager;
class ApplicationSettings;


class TransactionsWidget : public TabWithShortcut
{
Q_OBJECT

public:
   TransactionsWidget(QWidget* parent = nullptr );
   ~TransactionsWidget() override;

   void init(const std::shared_ptr<WalletsManager> &, const std::shared_ptr<ArmoryConnection> &
      , const std::shared_ptr<SignContainer> &);
   void SetTransactionsModel(const std::shared_ptr<TransactionsViewModel> &);
   void setAppSettings(std::shared_ptr<ApplicationSettings> appSettings);

   void shortcutActivated(ShortcutType s) override;

private slots:
   void showTransactionDetails(const QModelIndex& index);
   void updateResultCount();
   void walletsChanged();
   void walletsFilterChanged(int index);
   void onEnterKeyInTrxPressed(const QModelIndex &index);
   void onDataLoaded(int count);
   void onCreateRBFDialog();
   void onCreateCPFPDialog();

private:
   std::unique_ptr<Ui::TransactionsWidget> ui;

private:
   std::shared_ptr<TransactionsViewModel> transactionsModel_;
   std::shared_ptr<WalletsManager>        walletsManager_;
   std::shared_ptr<SignContainer>         signContainer_;
   std::shared_ptr<ArmoryConnection>      armory_;
   std::shared_ptr<ApplicationSettings>   appSettings_;
   TransactionsSortFilterModel         *  sortFilterModel_;
   QMenu    contextMenu_;
   QAction  *actionCopyAddr_ = nullptr;
   QAction  *actionRBF_ = nullptr;
   QAction  *actionCPFP_ = nullptr;
   QString  curAddress_;
};


#endif // __TRANSACTIONS_WIDGET_UI_H__
