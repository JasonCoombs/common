/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "RecipientContainer.h"
#include <QtGlobal>
#include "ScriptRecipient.h"
#include "BTCNumericTypes.h"
#include "Wallets.h"

RecipientContainer::RecipientContainer()
{}

bool RecipientContainer::IsReady() const
{
   return !xbtAmount_.isZero() && !address_.empty();
}

bool RecipientContainer::SetAddress(const bs::Address &address)
{
   address_ = address;
   return true;
}

void RecipientContainer::ResetAddress()
{
   address_.clear();
}

bs::Address RecipientContainer::GetAddress() const
{
   if (!address_.empty()) {
      return address_;
   }
   return bs::Address();
}

bool RecipientContainer::SetAmount(const bs::XBTAmount &amount, bool isMax)
{
   if ((xbtAmount_ == amount) && (isMax_ == isMax)) {
      return false;
   }
   xbtAmount_ = amount;
   isMax_ = isMax;
   return true;
}

bs::XBTAmount RecipientContainer::GetAmount() const
{
   return xbtAmount_;
}

std::shared_ptr<Armory::Signer::ScriptRecipient> RecipientContainer::GetScriptRecipient() const
{
   if (!IsReady()) {
      return nullptr;
   }
   return address_.getRecipient(xbtAmount_);
}
