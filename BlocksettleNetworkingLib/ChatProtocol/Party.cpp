#include "Party.h"

#include <QUuid>

namespace Chat
{

   Party::Party()
   {
      // default states
      std::string uuid = QUuid::createUuid().toString().toStdString();
      setId(uuid);
      setPartyType(PartyType::GLOBAL);
      setPartySubType(PartySubType::STANDARD);
   }

   Party::Party(const PartyType& partyType, const PartySubType& partySubType, const PartyState& partyState)
   {
      setId(QUuid::createUuid().toString().toStdString());
      setPartyType(partyType);
      setPartySubType(partySubType);
      setPartyState(partyState);
   }

   Party::Party(const std::string& id, const PartyType& partyType, const PartySubType& partySubType, const PartyState& partyState)
   {
      setId(id);
      setPartyType(partyType);
      setPartySubType(partySubType);
      setPartyState(partyState);
   }
}
