#ifndef __COMMON_STATUS_CODE_DEFINITIONS_H__
#define __COMMON_STATUS_CODE_DEFINITIONS_H__

namespace bs {
   namespace network {
      enum class SubmitWhitelistedAddressStatus : int
      {
         NoError,
         InternalStorageError,
         InvalidAddressFormat,
         AddressTooLong,
         DescriptionTooLong,
         AddressAlreadyUsed,
         WhitelistedAddressLimitExceeded
      };

      enum class RevokeWhitelistedAddressStatus : int
      {
         NoError,
         InternalStorageError,
         AddressNotFound,
         CouldNotRemoveLast
      };

      enum class WithdrawXbtStatus : int
      {
         NoError,
         CashReservationFailed,
         TxCreateFailed,
         TxBroadcastFailed
      };
   }
}

#endif // __COMMON_STATUS_CODE_DEFINITIONS_H__
