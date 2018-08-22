#ifndef __COIN_CONTROL_WIDGET_H__
#define __COIN_CONTROL_WIDGET_H__

#include <QWidget>
#include <memory>
#include "WalletsManager.h"

namespace Ui {
    class CoinControlWidget;
};

class CoinControlModel;
class SelectedTransactionInputs;

class CoinControlWidget : public QWidget
{
Q_OBJECT
public:
   CoinControlWidget(QWidget* parent = nullptr );
   ~CoinControlWidget() override;

   void initWidget(const std::shared_ptr<SelectedTransactionInputs> &);
   void applyChanges(const std::shared_ptr<SelectedTransactionInputs> &);

signals:
   void coinSelectionChanged(size_t currentlySelected, bool autoSelection);

private slots:
   void updateSelectedTotals();
   void onAutoSelClicked(int state);
   void rowClicked(const QModelIndex &index);

private:
   std::unique_ptr<Ui::CoinControlWidget> ui_;

   CoinControlModel *coinControlModel_;
};


#include <QPainter>
#include <QHeaderView>
#include <QStyleOptionButton>
#include <QMouseEvent>
#ifndef MAXSIZE_T
#  define MAXSIZE_T  ((size_t)-1)
#endif

class CCHeader : public QHeaderView
{
   Q_OBJECT
public:
   CCHeader(size_t totalTxCount, Qt::Orientation orient, QWidget *parent = nullptr)
      : QHeaderView(orient, parent), totalTxCount_(totalTxCount) {}

protected:
   void paintSection(QPainter *painter, const QRect &rect, int logIndex) const {
      painter->save();
      QHeaderView::paintSection(painter, rect, logIndex);
      painter->restore();

      if (logIndex == 0) {
         QStyleOptionButton option;
         const QSize ch = checkboxSizeHint();
         option.rect = QRect(2, (height() - ch.height()) / 2, ch.width(), ch.height());

         switch (state_)
         {
         case Qt::Checked:
            option.state = QStyle::State_Enabled | QStyle::State_On;
            break;
         case Qt::Unchecked:
            option.state = QStyle::State_Enabled | QStyle::State_Off;
            break;
         case Qt::PartiallyChecked:
         default:
            option.state = QStyle::State_Enabled | QStyle::State_NoChange;
            break;
         }
         style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, painter);
      }
   }

   QSize sectionSizeFromContents(int logicalIndex) const override
   {
      if (logicalIndex == 0) {
         const QSize orig = QHeaderView::sectionSizeFromContents(logicalIndex);
         const QSize checkbox = checkboxSizeHint();

         return QSize(orig.width() + checkbox.width() + 4,
                      qMax(orig.height(), checkbox.height() + 4));
      } else {
         return QHeaderView::sectionSizeFromContents(logicalIndex);
      }
   }

   void mousePressEvent(QMouseEvent *event) {
      if (QRect(0, 0, checkboxSizeHint().width() + 4, height()).contains(event->x(), event->y())) {
         if (state_ == Qt::Unchecked) {
            state_ = Qt::Checked;
         }
         else {
            state_ = Qt::Unchecked;
         }
         emit stateChanged(state_);
         update();
      } else {
         QHeaderView::mousePressEvent(event);
      }
   }

private:
   QSize checkboxSizeHint() const
   {
      QStyleOptionButton opt;
      return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt).size();
   }

private:
   Qt::CheckState state_ = Qt::Unchecked;
   size_t         totalTxCount_;

signals:
   void stateChanged(int);

public slots:
   void onSelectionChanged(size_t nbSelected, bool) {
      if (!nbSelected) {
         state_ = Qt::Unchecked;
      }
      else if (nbSelected < totalTxCount_) {
         state_ = Qt::PartiallyChecked;
      }
      else if ((nbSelected == totalTxCount_) || (nbSelected == MAXSIZE_T)) {
         state_ = Qt::Checked;
      }
      update();
   }
};

#endif // __COIN_CONTROL_WIDGET_H__
