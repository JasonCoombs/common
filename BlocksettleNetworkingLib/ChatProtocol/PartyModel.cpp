#include "PartyModel.h"
#include "FastLock.h"

#include <disable_warnings.h>
#include <spdlog/logger.h>
#include <enable_warnings.h>
#include <utility>

#include "ChatProtocol/PrivateDirectMessageParty.h"

using namespace Chat;

PartyModel::PartyModel(LoggerPtr loggerPtr, QObject* parent /* = nullptr */)
   : QObject(parent), loggerPtr_(std::move(loggerPtr))
{
   connect(this, &PartyModel::error, this, &PartyModel::handleLocalErrors);
}

void PartyModel::insertParty(const PartyPtr& partyPtr)
{
   PartyPtr oldPartyPtr{};

   {
      FastLock locker(partyMapLockerFlag_);
      const auto it = partyMap_.find(partyPtr->id());
      if (it != partyMap_.end())
      {
         oldPartyPtr = partyMap_[partyPtr->id()];
         partyMap_.erase(partyPtr->id());
         emit error(PartyModelError::InsertExistingParty, partyPtr->id(), true);
      }

      partyMap_[partyPtr->id()] = partyPtr;
   }

   if (oldPartyPtr)
   {
      emit partyRemoved(oldPartyPtr);
   }

   emit partyInserted(partyPtr);
   emit partyModelChanged();
}

void PartyModel::removeParty(const PartyPtr& partyPtr)
{
   PartyPtr oldPartyPtr{};
   auto isErased = false;

   {
      FastLock locker(partyMapLockerFlag_);
      const auto it = partyMap_.find(partyPtr->id());
      if (it != partyMap_.end())
      {
         oldPartyPtr = partyMap_[partyPtr->id()];
         partyMap_.erase(partyPtr->id());
         isErased = true;
      }
   }

   if (oldPartyPtr)
   {
      emit partyRemoved(oldPartyPtr);
   }

   if (isErased)
   {
      emit error(PartyModelError::RemovingNonexistingParty, partyPtr->id(), true);
      emit partyModelChanged();
   }
}

PartyPtr PartyModel::getPartyById(const std::string& party_id)
{
   {
      FastLock locker(partyMapLockerFlag_);

      const auto it = partyMap_.find(party_id);

      if (it != partyMap_.end())
      {
         return it->second;
      }      
   }

   emit error(PartyModelError::CouldNotFindParty, party_id, true);

   return nullptr;
}

PrivateDirectMessagePartyPtr PartyModel::getPrivatePartyById(const std::string& party_id)
{
   const auto partyPtr = getPartyById(party_id);

   if (!partyPtr)
   {
      emit error(PartyModelError::CouldNotFindParty, party_id, true);
      return nullptr;
   }

   auto privateDMPartyPtr = std::dynamic_pointer_cast<PrivateDirectMessageParty>(partyPtr);

   if (nullptr == privateDMPartyPtr)
   {
      // this should not happen
      emit error(PartyModelError::PrivatePartyCasting, party_id, true);
      return nullptr;
   }

   return privateDMPartyPtr;
}

void PartyModel::handleLocalErrors(const Chat::PartyModelError& errorCode, const std::string& what, bool displayAsWarning) const
{
   const auto displayAs = displayAsWarning ? ErrorType::WarningDescription : ErrorType::ErrorDescription;

   loggerPtr_->debug("[PartyModel::handleLocalErrors] {}: {}, what: {}", displayAs, static_cast<int>(errorCode), what);
}

void PartyModel::clearModel()
{
   for (const auto& element : partyMap_)
   {
      removeParty(element.second);
   }
}

void PartyModel::insertOrUpdateParty(const PartyPtr& partyPtr)
{
   // private party
   if (partyPtr->isPrivate())
   {
      const auto privatePartyPtr = std::dynamic_pointer_cast<PrivateDirectMessageParty>(partyPtr);

      if (nullptr == privatePartyPtr)
      {
         emit error(PartyModelError::DynamicPointerCast, partyPtr->id(), true);
         return;
      }

      auto existingPartyPtr = getPrivatePartyById(partyPtr->id());

      // party not exist, insert
      if (nullptr == existingPartyPtr)
      {
         insertParty(privatePartyPtr);
         return;
      }

      // party exist, update
      for (const auto &recipientPtr : privatePartyPtr->recipients())
      {
         existingPartyPtr->insertOrUpdateRecipient(recipientPtr);
      }
      return;
   }

   // other party types
   const auto existingPartyPtr = getPartyById(partyPtr->id());

   // if not exist, insert new, otherwise do nothing
   if (nullptr == existingPartyPtr)
   {
      insertParty(partyPtr);
   }
}

