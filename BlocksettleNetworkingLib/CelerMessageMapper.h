#ifndef __CELER_MESSAGE_MAPPER_H__
#define __CELER_MESSAGE_MAPPER_H__

#include <string>

namespace CelerAPI
{

enum CelerMessageType
{
   CelerMessageTypeFirst,
   ConnectedEventType = CelerMessageTypeFirst,
   CreateApiSessionRequestType,
   CreateStandardUserType,
   CreateUserPropertyRequestType,
   UpdateUserPropertyRequestType,
   FindAllSocketsType,
   FindStandardUserType,
   GenerateResetUserPasswordTokenRequestType,
   HeartbeatType,
   LoginRequestType,
   LoginResponseType,
   LogoutMessageType,
   MultiResponseMessageType,
   ReconnectionFailedMessageType,
   ReconnectionRequestType,
   SingleResponseMessageType,
   SocketConfigurationDownstreamEventType,
   StandardUserDownstreamEventType,
   ResetUserPasswordTokenType,
   ChangeUserPasswordRequestType,
   ChangeUserPasswordConfirmationType,
   FindUserPropertyByUsernameAndKeyType,
   UserPropertyDownstreamEventType,
   QuoteUpstreamType,
   QuoteNotificationType,
   QuoteDownstreamEventType,
   QuoteRequestNotificationType,
   QuoteRequestRejectDownstreamEventType,
   QuoteCancelRequestType,
   QuoteCancelNotificationType,
   QuoteCancelNotifReplyType,
   QuoteAckDownstreamEventType,
   CreateBitcoinOrderRequestType,
   CancelOrderRequestType,
   BitcoinOrderSnapshotDownstreamEventType,
   CreateFxOrderRequestType,
   FxOrderSnapshotDownstreamEventType,
   CreateOrderRequestRejectDownstreamEventType,
   QuoteCancelDownstreamEventType,
   FindAllAccountsType,
   GetAllAccountBalanceSnapshotType,
   AccountBulkUpdateDownstreamEventType,
   AccountBulkUpdateAcknowledgementType,
   AccountDownstreamEventType,
   AllSettlementAccountSnapshotDownstreamEventType,
   AccoutBalanceUpdatedDownstreamEventType,

   ProcessedFxTradeCaptureReportDownstreamEventType,
   ProcessedTradeCaptureReportAckType,

   SubscribeToTerminalRequestType,
   VerifiedAddressListUpdateEventType,
   SubscribeToTerminalResponseType,
   SessionEndedEventType,

   FindAssignedUserAccountsType,
   FindAllOrdersType,
   UserAccountDownstreamEventType,
   FindAllSubLedgersByAccountType,
   SubLedgerSnapshotDownstreamEventType,
   TransactionDownstreamEventType,

   VerifyXBTQuoteType,
   VerifyXBTQuoteRequestType,

   VerifyAuthenticationAddressResponseType,
   VerifyXBTQuoteResponseType,
   VerifyXBTQuoteRequestResponseType,

   ReserveCashForXBTRequestType,
   ReserveCashForXBTResponseType,

   XBTTradeRequestType,
   XBTTradeResponseType,

   FxTradeRequestType,
   FxTradeResponseType,

   ColouredCoinTradeRequestType,
   ColouredCoinTradeResponseType,

   VerifyColouredCoinQuoteType,
   VerifyColouredCoinQuoteRequestType,
   VerifyColouredCoinAcceptedQuoteType,

   VerifyColouredCoinQuoteResponseType,
   VerifyColouredCoinQuoteRequestResponseType,
   VerifyColouredCoinAcceptedQuoteResponseType,

   SignTransactionNotificationType,
   SignTransactionRequestType,

   EndOfDayPriceReportType,

   XBTTradeStatusRequestType,
   ColouredCoinTradeStatusRequestType,

   PersistenceExceptionType,

   MarketDataRequestRejectDownstreamEventType,
   MarketDataFullSnapshotDownstreamEventType,
   MarketDataIncrementalDownstreamEventType,

   MarketDataSubscriptionRequestType,

   CelerMessageTypeLast,
   UndefinedType = CelerMessageTypeLast
};

std::string GetMessageClass(CelerMessageType messageType);

CelerMessageType GetMessageType(const std::string& fullClassName);
};

#endif // __CELER_MESSAGE_MAPPER_H__
