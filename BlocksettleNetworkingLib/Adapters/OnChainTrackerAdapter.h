/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef ONCHAIN_TRACKER_ADAPTER_H
#define ONCHAIN_TRACKER_ADAPTER_H

#include "Message/ThreadedAdapter.h"
#include "AuthAddressLogic.h"

namespace spdlog {
   class logger;
}
namespace BlockSettle {
   namespace Common {
      class ArmoryMessage_OutpointsForAddrList;
      class ArmoryMessage_State;
      class ArmoryMessage_UTXOs;
      class ArmoryMessage_WalletRegistered;
      class OnChainTrackMessage_AuthAddresses;
      class WalletsMessage_WalletData;
   }
}
class AddrVerificatorCallbacks;
class CcTrackerClient;
class OnChainTrackerAdapter;

class OnChainExternalPlug
{
   friend class OnChainTrackerAdapter;
public:
   OnChainExternalPlug(const std::shared_ptr<bs::message::QueueInterface>& queue)
      : queue_(queue) {}

   virtual bool tryProcess(const bs::message::Envelope &env) = 0;
   virtual void sendAuthValidationListRequest() = 0;

protected:
   std::shared_ptr<bs::message::QueueInterface> queue_;
   OnChainTrackerAdapter* parent_{ nullptr };
   std::shared_ptr<bs::message::User>  user_;

private: // is kept for simplicity atm - can be replaced by setting callbacks later:
   void setParent(OnChainTrackerAdapter* parent
      , const std::shared_ptr<bs::message::User> &user)
   {
      parent_ = parent;
      user_ = user;
   }
};


class OnChainTrackerAdapter : public bs::message::ThreadedAdapter
{
   friend class AddrVerificatorCallbacks;
public:
   OnChainTrackerAdapter(const std::shared_ptr<spdlog::logger>&
      , const std::shared_ptr<bs::message::User>& ownUser
      , const std::shared_ptr<bs::message::User>& userBlockchain
      , const std::shared_ptr<bs::message::User>& userWallet
      , const std::shared_ptr<OnChainExternalPlug>&);
   ~OnChainTrackerAdapter() override;

   bool processEnvelope(const bs::message::Envelope &) override;

   std::set<std::shared_ptr<bs::message::User>> supportedReceivers() const override {
      return { user_ };
   }
   std::string name() const override { return "On-chain Tracker"; }

   void onAuthValidationAddresses(const std::vector<std::string> &);
   void onStart();

private:
   void connectAuthVerificator();
   void authAddressVerification();
   void completeAuthVerification(const bs::Address&, AddressVerificationState);

   bool processArmoryState(const BlockSettle::Common::ArmoryMessage_State&);
   bool processNewBlock(uint32_t);
   bool processWalletRegistered(uint64_t msgId
      , const BlockSettle::Common::ArmoryMessage_WalletRegistered&);
   bool processOutpoints(uint64_t msgId
      , const BlockSettle::Common::ArmoryMessage_OutpointsForAddrList&);
   bool processUTXOs(uint64_t msgId, const BlockSettle::Common::ArmoryMessage_UTXOs&);

   bool processAuthWallet(const BlockSettle::Common::WalletsMessage_WalletData&);
   bool processAuthAddresses(const BlockSettle::Common::OnChainTrackMessage_AuthAddresses&);
   void sendVerifiedAuthAddresses();

private:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<bs::message::User>     user_, userBlockchain_, userWallet_;
   std::shared_ptr<OnChainExternalPlug>   extPlug_;
   std::shared_ptr<CcTrackerClient>       ccTracker_;

   std::recursive_mutex mutex_;
   std::unique_ptr<AuthAddressValidator>     authVerificator_;
   std::shared_ptr<AuthValidatorCallbacks>   authCallbacks_;
   std::set<bs::Address>                     userAddresses_;
   std::map<bs::Address, AddressVerificationState> addrStates_;
   bool     blockchainReady_{ false };
   std::atomic_bool authOnline_{ false };
   uint32_t topBlock_{ 0 };
   std::map<uint64_t, AuthValidatorCallbacks::OutpointsCb>  outpointCallbacks_;
   std::map<uint64_t, AuthValidatorCallbacks::UTXOsCb>      utxoCallbacks_;

};


#endif	// ONCHAIN_TRACKER_ADAPTER_H
