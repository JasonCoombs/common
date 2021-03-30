#ifndef __SLACK_NOTIFICATION_MANAGER_H__
#define __SLACK_NOTIFICATION_MANAGER_H__

#include <memory>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include "NotificationManager.h"
#include "ProcessingThread.h"


namespace bs {
   namespace notification {

      struct SlackSettings
      {
         std::string host;
         std::string path;
         std::string hookURL;
      };


      template<typename T>
      class SlackManager : public Manager<T>, private ProcessingThread<std::string>
      {
      public:
         SlackManager(const std::shared_ptr<spdlog::logger>& logger, const SlackSettings& settings)
            : logger_{ logger }, sslCtx_{ CreateSSLCtx() }
            , settings_{ settings }
         {}
         ~SlackManager() noexcept override = default;

         SlackManager(const SlackManager&) = delete;
         SlackManager& operator = (const SlackManager&) = delete;

         SlackManager(SlackManager&&) = delete;
         SlackManager& operator = (SlackManager&&) = delete;

      private:
         bool sendRawNotification(const std::string& message) override final;
         void processPacket(const std::string& data) override final;

      private:
         using ssl_ctx_ptr = std::unique_ptr<SSL_CTX, void (*)(SSL_CTX*)>;
         using ssl_ptr = std::unique_ptr<SSL, void (*)(SSL*)>;

         static ssl_ctx_ptr CreateSSLCtx();

      private:
         std::shared_ptr<spdlog::logger>  logger_;
         ssl_ctx_ptr                      sslCtx_;
         SlackSettings                    settings_;
      };

   }  // namespace notification
}  // namespace bs

#endif // __SLACK_NOTIFICATION_MANAGER_H__
