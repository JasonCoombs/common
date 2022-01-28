/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PARTYRECIPIENT_H
#define PARTYRECIPIENT_H

#include <unordered_map>
#include <memory>
#include <vector>
#include <QDateTime>
#include <disable_warnings.h>
#include "BinaryData.h"
#include <enable_warnings.h>

namespace Chat
{

   class PartyRecipient
   {
   public:
      PartyRecipient(std::string userHash, BinaryData publicKey = BinaryData(), QDateTime publicKeyTime = QDateTime::currentDateTime());

      std::string userHash() const { return userHash_; }
      void setUserHash(const std::string& val) { userHash_ = val; }

      BinaryData publicKey() const { return publicKey_; }
      void setPublicKey(const BinaryData& val) { publicKey_ = val; }

      QDateTime publicKeyTime() const { return publicKeyTime_; }
      void setPublicKeyTime(const QDateTime& val) { publicKeyTime_ = val; }

   private:
      std::string userHash_;
      BinaryData publicKey_;
      QDateTime publicKeyTime_;
   };

   using PartyRecipientPtr = std::shared_ptr<PartyRecipient>;
   using PartyRecipientsPtrList = std::vector<PartyRecipientPtr>;
   using UniqieRecipientMap = std::unordered_map<std::string, PartyRecipientPtr>;

}

#endif // PARTYRECIPIENT_H
