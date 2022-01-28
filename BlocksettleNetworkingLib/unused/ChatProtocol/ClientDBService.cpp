/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ChatProtocol/ClientDBService.h"

using namespace Chat;

ClientDBService::ClientDBService(QObject* parent /* = nullptr */)
   : ServiceThread<ClientDBLogic>(new ClientDBLogic, parent)
{
   qRegisterMetaType<Chat::CryptManagerPtr>();
   qRegisterMetaType<Chat::PartyRecipientsPtrList>();
   qRegisterMetaType<Chat::UniqieRecipientMap>();
   qRegisterMetaType<Chat::PartyPtr>();

   ////////// PROXY SIGNALS //////////
   connect(this, &ClientDBService::Init, worker(), &ClientDBLogic::Init);
   connect(this, &ClientDBService::saveMessage, worker(), &ClientDBLogic::saveMessage);
   connect(this, &ClientDBService::updateMessageState, worker(), &ClientDBLogic::updateMessageState);
   connect(this, &ClientDBService::createNewParty, worker(), &ClientDBLogic::createNewParty);
   connect(this, &ClientDBService::readUnsentMessages, worker(), &ClientDBLogic::readUnsentMessages);
   connect(this, &ClientDBService::updateDisplayNameForParty, worker(), &ClientDBLogic::updateDisplayNameForParty);
   connect(this, &ClientDBService::loadPartyDisplayName, worker(), &ClientDBLogic::loadPartyDisplayName);
   connect(this, &ClientDBService::checkUnsentMessages, worker(), &ClientDBLogic::checkUnsentMessages);
   connect(this, &ClientDBService::readHistoryMessages, worker(), &ClientDBLogic::readPrivateHistoryMessages);
   connect(this, &ClientDBService::saveRecipientsKeys, worker(), &ClientDBLogic::saveRecipientsKeys);
   connect(this, &ClientDBService::deleteRecipientsKeys, worker(), &ClientDBLogic::deleteRecipientsKeys);
   connect(this, &ClientDBService::updateRecipientKeys, worker(), &ClientDBLogic::updateRecipientKeys);
   connect(this, &ClientDBService::checkRecipientPublicKey, worker(), &ClientDBLogic::checkRecipientPublicKey);
   connect(this, &ClientDBService::cleanUnusedParties, worker(), &ClientDBLogic::clearUnusedParties);
   connect(this, &ClientDBService::savePartyRecipients, worker(), &ClientDBLogic::savePartyRecipients);
   connect(this, &ClientDBService::requestPrivateMessagesHistoryCount, worker(), &ClientDBLogic::requestPrivateMessagesHistoryCount);
   connect(this, &ClientDBService::requestAllHistoryMessages, worker(), &ClientDBLogic::requestAllHistoryMessages);

   ////////// RETURN SIGNALS //////////
   connect(worker(), &ClientDBLogic::initDone, this, &ClientDBService::initDone);
   connect(worker(), &ClientDBLogic::messageArrived, this, &ClientDBService::messageArrived);
   connect(worker(), &ClientDBLogic::messageStateChanged, this, &ClientDBService::messageStateChanged);
   connect(worker(), &ClientDBLogic::messageLoaded, this, &ClientDBService::messageLoaded);
   connect(worker(), &ClientDBLogic::partyDisplayNameLoaded, this, &ClientDBService::partyDisplayNameLoaded);
   connect(worker(), &ClientDBLogic::unsentMessagesFound, this, &ClientDBService::unsentMessagesFound);
   connect(worker(), &ClientDBLogic::recipientKeysHasChanged, this, &ClientDBService::recipientKeysHasChanged);
   connect(worker(), &ClientDBLogic::recipientKeysUnchanged, this, &ClientDBService::recipientKeysUnchanged);
   connect(worker(), &ClientDBLogic::privateMessagesHistoryCount, this, &ClientDBService::privateMessagesHistoryCount);
}
