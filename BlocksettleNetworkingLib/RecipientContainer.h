/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __RECIPIENT_CONTAINER_H__
#define __RECIPIENT_CONTAINER_H__

#include "Address.h"
#include "XBTAmount.h"

#include <string>
#include <memory>

class AddressEntry;
class BinaryData;

namespace ArmorySigner
{
   class ScriptRecipient;
};

class RecipientContainer
{
public:
   RecipientContainer();
   ~RecipientContainer() noexcept = default;

   RecipientContainer(const RecipientContainer&) = delete;
   RecipientContainer& operator = (const RecipientContainer&) = delete;
   RecipientContainer(RecipientContainer&&) = delete;
   RecipientContainer& operator = (RecipientContainer&&) = delete;

public:
   bool IsReady() const;

   bool SetAddress(const bs::Address& address);
   bs::Address GetAddress() const;
   void ResetAddress();

   bool SetAmount(const bs::XBTAmount &, bool isMax = false);
   bs::XBTAmount GetAmount() const;
   bool IsMaxAmount() const { return isMax_; }

   std::shared_ptr<Armory::Signer::ScriptRecipient> GetScriptRecipient() const;

private:
   bs::Address    address_;
   bs::XBTAmount  xbtAmount_;
   bool           isMax_{false};
};

#endif // __RECIPIENT_CONTAINER_H__
