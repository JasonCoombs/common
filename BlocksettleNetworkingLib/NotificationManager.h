#ifndef __NOTIFICATION_MANAGER_H__
#define __NOTIFICATION_MANAGER_H__

#include <string>
#include <map>

namespace bs {
   namespace notification {

      // T - notification type
      // D - notification data
      template<typename T, typename D = std::string>
      class Manager
      {
      public:
         using self_type = Manager<T, D>;
      public:
         Manager() = default;
         virtual ~Manager() noexcept = default;

         Manager(const self_type&) = delete;
         Manager& operator = (const self_type&) = delete;

         Manager(self_type&&) = delete;
         Manager& operator = (self_type&&) = delete;

         // do not send same message for same type multiple times in a row
         bool SendUpdatedMessage(const T& messageType, const D& messageData)
         {
            const std::string rawData = messageData;
            auto it = lastMessages_.find(messageType);

            if (it == lastMessages_.end() || it->second != rawData) {
               lastMessages_[messageType] = rawData;
               return sendRawNotification(rawData);
            }

            return true;
         }

         bool PostAlways(const D& messageData)
         {
            const std::string rawData = messageData;
            return sendRawNotification(rawData);
         }

      protected:
         virtual bool sendRawNotification(const std::string& message) = 0;

      private:
         std::map<T, std::string> lastMessages_;
      };

   }  // namespace notification
}     // namespace bs

#endif // __NOTIFICATION_MANAGER_H__
