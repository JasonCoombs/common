#include "HttpsConnection.h"
#ifdef WIN32
#include <Winsock2.h>
#else // WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <unordered_set>
#include <openssl/err.h>
#include <spdlog/spdlog.h>
#include "BinaryData.h"

class SSLDisconnectedException : public std::runtime_error
{
public:
   SSLDisconnectedException(const std::string& what = {}) : std::runtime_error(what) {}
};

HttpsConnection::HttpsConnection(const std::shared_ptr<spdlog::logger> &logger
   , const std::string &host)
   : logger_(logger), host_(host)
{
   connectSocket();

   std::thread([this] {
      while (!stopped_ && inRequest_) {
         try {
            const auto &data = readSocket();
            process(data);
            if (data.empty()) {
               std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
            }
         }
         catch (const SSLDisconnectedException&) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
         }
         catch (const std::exception &e) {
            logger_->error("[HttpsConnection] {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });
         }
      }
   }).detach();
}

HttpsConnection::~HttpsConnection()
{
   stopped_ = true;
   disconnectSocket();
}

void HttpsConnection::connectSocket()
{
#ifdef WIN32
   SOCKET s;
#else
   int s;
#endif
   s = socket(AF_INET, SOCK_STREAM, 0);
   if (s < 0) {
      throw std::runtime_error("error creating SSL socket");
   }

   struct hostent* he = nullptr;
   he = gethostbyname(host_.c_str());
   if (!he) {
      throw std::runtime_error("host resolution error");
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
      throw std::runtime_error("error connecting to server");
   }
   const SSL_METHOD* meth = TLS_client_method();  //TLSv1_2_client_method();
   ctx_ = SSL_CTX_new(meth);
   ssl_ = SSL_new(ctx_);
   if (!ssl_) {
      throw std::runtime_error("error creating SSL");
   }
#ifdef WIN32
   SSL_set_fd(ssl_, (int)s);
#else
   SSL_set_fd(ssl_, s);
#endif

   SSL_set_mode(ssl_, SSL_MODE_AUTO_RETRY);
   SSL_set_tlsext_host_name(ssl_, host_.c_str());

   int err = SSL_connect(ssl_);
   if (err <= 0) {
      throw std::runtime_error("error " + std::to_string(SSL_get_error(ssl_, err)) + " creating HTTPS connection to " + host_);
   }
   logger_->info("[HttpsConnection] SSL to {} using {}", host_, SSL_get_cipher(ssl_));
}

void HttpsConnection::disconnectSocket()
{
   if (ssl_) {
      close(SSL_get_fd(ssl_));
      SSL_free(ssl_);
   }
   ssl_ = nullptr;

   if (ctx_) {
      SSL_CTX_free(ctx_);
   }
   ctx_ = nullptr;
}

std::string HttpsConnection::readSocket()
{
   if (!ssl_) {
      throw SSLDisconnectedException();
   }
   char buf[8 * 1024];
   int len = 0;
   len = SSL_read(ssl_, buf, sizeof(buf));
   if (len <= 0) {
      if (!ssl_) {
         throw SSLDisconnectedException();
      }
      const int err = SSL_get_error(ssl_, len);
      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
         logger_->warn("[{}] SSL read error {}", __func__, err);
         [[fallthrough]];
      case SSL_ERROR_ZERO_RETURN:
         break;
//      case SSL_ERROR_ZERO_RETURN:
      case SSL_ERROR_SYSCALL:
      case SSL_ERROR_SSL:
         throw std::runtime_error("SSL read error " + std::to_string(err));
      default: break;
      }
      return {};
   }
   return std::string(buf, len);
}

void HttpsConnection::sendRequest(const std::string &data)
{
   logger_->debug("[HttpsConnection] sending request:\n{}", data);
   int len = 0;
   while (len < (int)data.length()) {
      if (!ssl_) {
         len = -1;
      }
      else {
         len = SSL_write(ssl_, data.c_str(), (int)data.length());
      }
      if (len < 0) {
         int err = SSL_ERROR_SSL;
         if (ssl_) {
            err = SSL_get_error(ssl_, len);
         }
         switch (err) {
         case SSL_ERROR_WANT_WRITE:
            //logger_->warn("[{}] SSL want write", __func__);
            continue;
         case SSL_ERROR_WANT_READ:
            //logger_->warn("[{}] SSL want read", __func__);
            continue;
         case SSL_ERROR_ZERO_RETURN:
         case SSL_ERROR_SYSCALL:
         case SSL_ERROR_SSL:
         default:
            logger_->warn("[{}] SSL write error {}", __func__, err);
            disconnectSocket();
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
            try {
               connectSocket();
            }
            catch (const std::exception& e) {
               logger_->error("[HttpsConnection] failed to connect: {}", e.what());
            }
            continue;   // attempt to re-try
            //throw std::runtime_error("SSL write error " + std::to_string(err));
         }
      }
      else if (len < data.length()) {
         logger_->warn("[HttpsConnection] sent {} bytes vs {}", len, data.length());
      }
   }
}

void HttpsConnection::sendGetRequest(const std::string &request
   , const std::vector<std::string>& additionalHeaders)
{
   std::string decoratedReq = "GET " + request + " HTTP/1.1\r\n";
   decoratedReq += "User-Agent: BlockSettle connector v" + std::string(version) + "\r\n";
   for (const auto& header : additionalHeaders) {
      decoratedReq += header + "\r\n";
   }
   decoratedReq += "\r\n";
   sendRequest(decoratedReq);
}

void HttpsConnection::sendPostRequest(const std::string &request, const std::string& body
   , const std::vector<std::string>& additionalHeaders)
{
   auto decoratedReq = "POST " + request + " HTTP/1.1\r\n";
   decoratedReq += "Host: " + host_ + "\r\n";
   decoratedReq += "User-Agent: BlockSettle connector v" + std::string(version) + "\r\n";
   decoratedReq += "Accept: */*\r\n";
   for (const auto& header : additionalHeaders) {
      decoratedReq += header + "\r\n";
   }
   decoratedReq += "\r\n";
   decoratedReq += body;
   sendRequest(decoratedReq);
}
