/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef CHATUSER_H
#define CHATUSER_H

#include <string>
#include <memory>

#include <QObject>

#include <disable_warnings.h>
#include "BinaryData.h"
#include "SecureBinaryData.h"
#include <enable_warnings.h>

#include "CommonTypes.h"

namespace Chat
{

   class ChatUser : public QObject
   {
      Q_OBJECT
   public:
      ChatUser(QObject *parent = nullptr);
      std::string userHash() const;
      void setUserHash(const std::string& userName);

      BinaryData publicKey() const { return publicKey_; }
      void setPublicKey(const BinaryData& val) { publicKey_ = val; }

      SecureBinaryData privateKey() const { return privateKey_; }
      void setPrivateKey(const SecureBinaryData& val) { privateKey_ = val; }

      bs::network::UserType celerUserType() const { return celerUserType_; }
      void setCelerUserType(const bs::network::UserType& val) { celerUserType_ = val; }
   signals:
      void userHashChanged(const std::string& userHash);

   private:
      std::string userHash_;
      BinaryData publicKey_;
      SecureBinaryData privateKey_;
      bs::network::UserType celerUserType_ = bs::network::UserType::Undefined;
   };

   using ChatUserPtr = std::shared_ptr<ChatUser>;
}

#endif // CHATUSER_H
