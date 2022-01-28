/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __PASSWORD_DIALOG_DATA_WRAPPER_H__
#define __PASSWORD_DIALOG_DATA_WRAPPER_H__

#include "PasswordDialogData.h"
#include "Blocksettle_Communication_Internal.pb.h"

namespace Blocksettle {
namespace Communication {
namespace Internal {

class PasswordDialogDataWrapper
{
public:
   PasswordDialogDataWrapper() {}

   // copy constructors and operator= uses parent implementation
   //PasswordDialogDataWrapper(const PasswordDialogData &seed) : data_(seed){}
   PasswordDialogDataWrapper(const PasswordDialogData &other) : data_(static_cast<PasswordDialogData>(other)) {}
   PasswordDialogDataWrapper& operator= (const PasswordDialogData &other) { data_ = other; return *this;}

   template<typename T>
   T value(const bs::sync::dialog::keys::Key &key) const noexcept
   {
      return value<T>(key.toString());
   }

   void insert(const bs::sync::dialog::keys::Key &key, bool value);
   void insert(const bs::sync::dialog::keys::Key &key, const std::string &value);
   void insert(const bs::sync::dialog::keys::Key &key, int value);
   void insert(const bs::sync::dialog::keys::Key &key, double value);
   void insert(const bs::sync::dialog::keys::Key &key, const char *data, size_t size);

   auto mutable_valuesmap() { return data_.mutable_valuesmap(); }
   auto data() const { return data_; }

private:
   template<typename T>
   T value(const std::string &key) const noexcept
   {
      try {
         if (!data_.valuesmap().contains(key)) {
            return T();
         }

         const google::protobuf::Any &msg = data_.valuesmap().at(key);
         Blocksettle::Communication::Internal::AnyMessage anyMsg;
         msg.UnpackTo(&anyMsg);

         return valueImpl<T>(anyMsg);
      } catch (...) {
         return  T();
      }
   }

   template<typename T>
   void insertImpl(const std::string &key, T value);

   template<typename T>
   T valueImpl(const AnyMessage &anyMsg) const;

private:
   PasswordDialogData   data_;
};


template<> bool PasswordDialogDataWrapper::valueImpl<bool>(const AnyMessage &anyMsg) const;
template<> std::string PasswordDialogDataWrapper::valueImpl<std::string>(const AnyMessage &anyMsg) const;
template<> int PasswordDialogDataWrapper::valueImpl<int>(const AnyMessage &anyMsg) const;
template<> double PasswordDialogDataWrapper::valueImpl<double>(const AnyMessage &anyMsg) const;
template<> const char * PasswordDialogDataWrapper::valueImpl<const char *>(const AnyMessage &anyMsg) const;

} // namespace Internal
} // namespace Communication
} // Blocksettle
#endif // __PASSWORD_DIALOG_DATA_WRAPPER_H__
