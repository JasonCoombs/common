#ifndef __SLACK_NOTIFICATION_MANAGER_H__
#define __SLACK_NOTIFICATION_MANAGER_H__

#ifdef WIN32
#include <Winsock2.h>
#include <Windows.h>
#else // WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <memory>
#include <nlohmann/json.hpp>
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
         bool sendRawNotification(const std::string& message) override final
         {  //define in header due to templated class
            SchedulePacketProcessing(message);
            return true;
         }

         void processPacket(const std::string& data) override final
         {  //define in header due to templated class
#ifdef WIN32
            SOCKET s;
#else
            int s;
#endif
            s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
               logger_->error("[SlackNotificationManager::processPacket] failed to create socket");
               haltProcessing();
               return;
            }

            const std::string& hostName = getHost();
            struct hostent* he = gethostbyname(hostName.c_str());
            if (!he) {
               logger_->error("[SlackNotificationManager::processPacket] failed to resolve host {}"
                  , hostName);
               haltProcessing();
               return;
            }
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = inet_addr(inet_ntoa(*((struct in_addr*)he->h_addr_list[0])));
            sa.sin_port = htons(443);
#ifdef WIN32
            int socklen = sizeof(sa);
#else
            socklen_t socklen = sizeof(sa);
#endif

            if (connect(s, (struct sockaddr*)&sa, socklen)) {
               logger_->error("[SlackNotificationManager::processPacket] connect failed");
               haltProcessing();
               return;
            }

            ssl_ptr sslSocket{ SSL_new(sslCtx_.get()), SSL_free };

            if (!sslSocket) {
               logger_->error("[SlackNotificationManager::processPacket] failed to create ssl socket");
               haltProcessing();
               return;
            }

#ifdef WIN32
            SSL_set_fd(sslSocket.get(), (int)s);
#else
            SSL_set_fd(sslSocket.get(), s);
#endif

            int err = SSL_connect(sslSocket.get());
            if (err <= 0) {
               logger_->error("[SlackNotificationManager::processPacket] SSL connect failed {}"
                  , SSL_get_error(sslSocket.get(), err));
               haltProcessing();
               return;
            }

            // create payload
            const nlohmann::json payload{ {"text", data} };
            const std::string payloadString = payload.dump();

            // create header
            std::string requestBody = "POST " + settings_.path + " HTTP/1.1\r\n";
            requestBody += "Host: " + settings_.host + "\r\n";
            requestBody += "Accept: */*\r\n";
            requestBody += "User-Agent: BlockSettle LP\r\n";
            requestBody += "Content-type: application/json\r\n";
            requestBody += "Content-Length: " + std::to_string(payloadString.length()) + "\r\n";
            requestBody += "\r\n";
            requestBody += payloadString;

            // send
            int len = SSL_write(sslSocket.get(), requestBody.c_str(), (int)requestBody.length());
            if (len < 0) {
               int err = SSL_get_error(sslSocket.get(), len);
               switch (err) {
               case SSL_ERROR_WANT_WRITE:
                  logger_->error("[SlackNotificationManager::processPacket] send SSL_ERROR_WANT_WRITE");
                  return;
               case SSL_ERROR_WANT_READ:
                  logger_->error("[SlackNotificationManager::processPacket] send SSL_ERROR_WANT_READ");
                  return;
               case SSL_ERROR_ZERO_RETURN:
               case SSL_ERROR_SYSCALL:
               case SSL_ERROR_SSL:
               default:
                  logger_->error("[SlackNotificationManager::processPacket] send error {}"
                     , err);
                  break;
               }
            }
            // do not wait for response
         }

      private:
         using ssl_ctx_ptr = std::unique_ptr<SSL_CTX, void (*)(SSL_CTX*)>;
         using ssl_ptr = std::unique_ptr<SSL, void (*)(SSL*)>;

         static void initOpenSSL()
         {
            static std::once_flag once;
            std::call_once(once, []() {
               SSL_library_init();
               SSLeay_add_ssl_algorithms();
               SSL_load_error_strings();
               OpenSSL_add_all_algorithms();
            });
         }

         [[nodiscard]] static ssl_ctx_ptr CreateSSLCtx()
         {
            initOpenSSL();
            ssl_ctx_ptr ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);

            // XXX: set cert validation?

            return ctx;
         }

         [[nodiscard]] static std::string getHost()
         {
            return "hooks.slack.com";
         }

      private:
         std::shared_ptr<spdlog::logger>  logger_;
         ssl_ctx_ptr                      sslCtx_;
         SlackSettings                    settings_;
      };

   }  // namespace notification
}  // namespace bs

#endif // __SLACK_NOTIFICATION_MANAGER_H__
